#ifdef __INTELLISENSE__
#pragma diag_suppress 29
#endif

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "compiler.h"
#include "common.h"
#include "debug.h"
#include "scanner.h"
#include "object.h"
#include "mem.h"
#include "natives.h"
#include "methods.h"
#include "vm.h"

VM vm;

// reset the stack
static void resetStack()
{
	vm.stackTop = vm.stack;
	vm.frameCount = 0;
	vm.openUpvalues = NULL;
}

// display a runtime error
void runtimeError(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	for (int i = vm.frameCount - 1; i >= 0; i--)
	{
		CallFrame *frame = &vm.frames[i];
		ObjFunction *function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;
		fprintf(stderr, "[line %d] in ",
				function->chunk.lines[instruction]);
		if (function->name == NULL)
		{
			fprintf(stderr, "script\n");
		}
		else
		{
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}
	resetStack();
}

void defineNativeFn(const char *name, NativeFn function, int arity)
{
	push(OBJ_VAL(copyString(name, (int)strlen(name))));
	push(OBJ_VAL(newNative(function, arity, name)));
	tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
	pop();
	pop();
}

static void defineNativeVars()
{
	int count = sizeof NativeVars / sizeof NativeVars[0];
	for (int i = 0; i < count; i++)
		tableSet(&vm.nativeVars, 
			copyString(NativeVars[i], strlen(NativeVars[i])), NULL_VAL);
}

// initialize the VM
void initVM(bool import_mode)
{
	resetStack();
	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;
	vm.objects = NULL;
	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;
	initTable(&vm.nativeVars);
	initTable(&vm.strings);
	initTable(&vm.globals);
	initTable(&vm.globalsTypes);

	vm.initString = NULL;
	vm.initString = copyString("Init", 4);

	if (!import_mode)
	{
		defineNatives();
		defineNativeVars();
		defineAllMethods();
	}
}

// free the VM
void freeVM()
{
	vm.initString = NULL;
	freeObjects();
	freeTable(&vm.nativeVars);
	freeTable(&vm.strings);
	freeTable(&vm.globals);
	freeTable(&vm.globalsTypes);
}

// push a new value onto the stack
void push(Value value)
{
	*vm.stackTop = value;
	vm.stackTop++;
}

// pop the last value off the stack
Value pop()
{
	vm.stackTop--;

	// printf("POPPING: ");
	// printValue(*vm.stackTop);
	// printf("\n");
	
	return *vm.stackTop;
}

// return the Value at distance from top of stack without popping
static Value peek(int distance)
{
	return vm.stackTop[-1 - distance];
}

static bool checkType(Value value, ObjDataType type, const char *format);

// calls the given function with the given argcount
static bool call(ObjClosure *closure, int argCount)
{
	if (argCount != closure->function->arity)
	{
		runtimeError("Expected %d arguments but got %d.",
					 closure->function->arity, argCount);
		return false;
	}

	// check arg types
	for (int i = argCount - 1; i >= 0 ; i--)
	{
		Value type = closure->function->argTypes.values[argCount - i - 1];
		if (!checkType(peek(i), *AS_DATA_TYPE(type), "Expected argument of type %s, not %s."))
			return false;
	}
	
	if (vm.frameCount == FRAMES_MAX)
	{
		runtimeError("Frame stack overflow.");
		return false;
	}

	CallFrame *frame = &vm.frames[vm.frameCount++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	frame->slots = vm.stackTop - argCount - 1;
	return true;
}

// attempts to call the given value with the given amount of args
bool callValue(Value callee, int argCount)
{
	if (IS_OBJ(callee))
	{
		switch (OBJ_TYPE(callee))
		{
		case OBJ_BOUND_METHOD:
		{
			ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
			// set the 'this' variable at slot 0 of the callframe
			vm.stackTop[-argCount - 1] = bound->receiver;
			return call(bound->method, argCount);
		}
		case OBJ_CLASS:
		{
			ObjClass *klass = AS_CLASS(callee);
			vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));

			// set fields
			ObjInstance *instance = AS_INSTANCE(vm.stackTop[-argCount - 1]);
			
			tableAddAll(&klass->fields, &instance->fields);
			tableAddAll(&klass->fieldsTypes, &instance->fieldsTypes);

			Value initializer;
			if (tableGet(&klass->methods, vm.initString, &initializer))
			{
				return call(AS_CLOSURE(initializer), argCount);
			}
			else if (argCount != 0)
			{
				// no init() so no args
				runtimeError("Expected 0 arguments but got %d.", argCount);
				return false;
			}

			return true;
		}
		case OBJ_CLOSURE:
			return call(AS_CLOSURE(callee), argCount);
		case OBJ_NATIVE:
		{
			// NativeFn native = AS_NATIVE(callee);
			ObjNative *native = AS_NATIVE(callee);

			// arity -1 indicates that the arity is handled by the native
			if (native->arity != -1 && argCount != native->arity)
			{
				runtimeError("Expected %d arguments but got %d.", native->arity, argCount);
				return false;
			}

			Value result = native->function(argCount, vm.stackTop - argCount);
			// -1 as result type marks runtimeError inside native
			if (result.type == -1)
				return false;

			vm.stackTop -= argCount + 1;
			push(result);
			return true;
		}
		case OBJ_BOUND_N_M:
		{
			ObjBoundNativeMethod *method = AS_BOUND_N_M(callee);
			push(method->receiver);
			argCount++;

			// arity -1 indicates that the arity is handled by the native
			if (method->native->arity != -1 && argCount != method->native->arity)
			{
				runtimeError("Expected %d arguments but got %d.", method->native->arity, argCount);
				return false;
			}

			Value result = method->native->function(argCount, vm.stackTop - argCount);
			// -1 as result type marks runtimeError inside native
			if (result.type == -1)
				return false;

			vm.stackTop -= argCount + 1;
			push(result);
			return true;
		}
		case OBJ_DATA_TYPE:
		{
			Value result = callDataType(AS_DATA_TYPE(callee), argCount, vm.stackTop - argCount);
			if (result.type == -1)
			{
				// result is actually an ObjString
				runtimeError(AS_CSTRING(result));
				return false;
			}
			push(result);
			return true;
		}
		default:
			break; // Non-callable object type.
		}
	}
	runtimeError("Can only call functions and classes.");
	return false;
}

static bool invokeFromClass(ObjClass *klass, ObjString *name, int argCount)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method))
	{
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}
	return call(AS_CLOSURE(method), argCount);
}

static bool bindNativeMethod(Value value, ObjString *name)
{
	// ObjBoundNativeMethod *method = newBoundNativeMethod();
	ValueArray *methods;
	bool arrayNotFound = false;

	// get array of methods of value type
	switch (value.type)
	{
	case VAL_NUMBER: methods = &numberMethods; break;
	case VAL_OBJ:
		switch (OBJ_TYPE(value))
		{
		case OBJ_STRING: methods = &stringMethods; break;
		case OBJ_ARRAY:  methods = &arrayMethods; break;
		default: arrayNotFound = true; break;
		}
		break;
	default: arrayNotFound = true; break;
	}

	if (arrayNotFound)
	{
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	// get method from array
	ObjNative *native;
	bool methodNotFound = true;
	for (int i = 0; i < methods->count; i++)
	{
		if (valuesEqual(OBJ_VAL(AS_NATIVE(methods->values[i])->name), OBJ_VAL(name)))
		{
			native = AS_NATIVE(methods->values[i]);
			methodNotFound = false;
			break;
		}
	}
	
	ObjBoundNativeMethod *method = newBoundNativeMethod(value, native);

	if (methodNotFound)
	{
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	push(OBJ_VAL(method));
	return true;
}

// shortcut that combines getting a method and calling it for performance
static bool invoke(ObjString *name, int argCount)
{
	Value receiver = peek(argCount);

	// if (!IS_INSTANCE(receiver))
	// {
	// 	runtimeError("Only instances have methods.");
	// 	return false;
	// }
	if (IS_INSTANCE(receiver))
	{
		ObjInstance *instance = AS_INSTANCE(receiver);

		// method can be stored inside a field as well
		Value value;
		if (tableGet(&instance->fields, name, &value))
		{
			vm.stackTop[-argCount - 1] = value;
			return callValue(value, argCount);
		}

		return invokeFromClass(instance->klass, name, argCount);
	}
	else
	{
		if (!bindNativeMethod(receiver, name))
			return false;
		return callValue(pop(), argCount);
	}
}

// looks up and binds the given method if it exists, otherwise 
// false is returned
static bool bindMethod(ObjClass *klass, ObjString *name)
{
	Value method;
	if (!tableGet(&klass->methods, name, &method))
	{
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));
	pop();
	push(OBJ_VAL(bound));
	return true;
}

// captures the given local as an upvalue and returns that
static ObjUpvalue *captureUpvalue(Value *local)
{
	ObjUpvalue *prevUpvalue = NULL;
	ObjUpvalue *upvalue = vm.openUpvalues;
	while (upvalue != NULL && upvalue->location > local)
	{
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == local)
	{
		return upvalue;
	}

	ObjUpvalue *createdUpvalue = newUpvalue(local);
	createdUpvalue->next = upvalue;

	if (prevUpvalue == NULL)
	{
		vm.openUpvalues = createdUpvalue;
	}
	else
	{
		prevUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

// closes over the upvalue
static void closeUpvalues(Value *last)
{
	while (vm.openUpvalues != NULL &&
		   vm.openUpvalues->location >= last)
	{
		ObjUpvalue *upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}

// add the method that's on top of the stack in the
// form of a closure to the class below it
static void defineMethod(ObjString *name)
{
	Value method = peek(0);
	ObjClass *klass = AS_CLASS(peek(1));
	tableSet(&klass->methods, name, method);
	pop();
}

// check wether the given value returns to false
bool isFalsey(Value value)
{
	// return IS_NULL(value) || (IS_BOOL(value) && !AS_BOOL(value));
	if (IS_NULL(value)) return true;
	
	else if (IS_BOOL(value))
		return !AS_BOOL(value);
	
	else if (IS_NUMBER(value))
		return AS_NUMBER(value) == 0;
	
	else if (IS_OBJ(value))
	{
		switch (value.as.obj->type)
		{
		case OBJ_STRING:
			return AS_STRING(value)->length == 0;
		case OBJ_UPVALUE:
			return isFalsey(*(((ObjUpvalue *)AS_OBJ(value))->location));
		// case OBJ_BOUND_METHOD:
		// case OBJ_CLASS:
		// case OBJ_CLOSURE:
		// case OBJ_FUNCTION:
		// case OBJ_INSTANCE:
		// case OBJ_NATIVE:
		// return true;
		default:
			return false; 
		}
	}

	return true;
}

// check if datatype is correct (example format: "Expect type %s, not %s.")
static bool checkType(Value value, ObjDataType type, const char* format)
{
	if (type.isAny) return true;

	if (value.type != type.valueType ||
		(IS_OBJ(value) && OBJ_TYPE(value) != type.objType))
	{
		runtimeError(format,
			objectToString(OBJ_VAL(&type)),
			objectToString(OBJ_VAL(newDataType(value, false))));
		return false;
	}
	return true;
}

// add two strings
static void concatenate()
{
	ObjString *b = AS_STRING(peek(0));
	ObjString *a = AS_STRING(peek(1));

	int length = a->length + b->length;
	char *chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString *result = takeString(chars, length);

	pop();
	pop();
	push(OBJ_VAL(result));
}








// run shit
static InterpretResult run(bool repl_mode)
{
	CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
	(frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op)                        \
	do                                                  \
	{                                                   \
		if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) \
		{                                               \
			runtimeError("Operands must be numbers.");  \
			return INTERPRET_RUNTIME_ERROR;             \
		}                                               \
		double b = AS_NUMBER(pop());                    \
		double a = AS_NUMBER(pop());                    \
		push(valueType(a op b));                        \
	} while (false)
	// wrap in block so that the macro expands safely

	for (;;)
	{
// debugging stuff
#ifdef DEBUG_TRACE_EXECUTION

		printf("\n\nSTACK:    ");
		for (Value *slot = vm.stack; slot < vm.stackTop; slot++)
		{
			printf("[");
			printValue(*slot);
			printf("]");
		}

		printf("\nINSTRUCT %d: ", (int)(frame->ip - frame->closure->function->chunk.code));
		disassembleInstruction(&frame->closure->function->chunk,
							   (int)(frame->ip - frame->closure->function->chunk.code));
		printf(">>> ");
#endif
		// swtich for execution of each intruction
		uint8_t instruction;
		switch (instruction = READ_BYTE())
		{
		case OP_CONSTANT:
		{
			Value constant = READ_CONSTANT();
			push(constant);
			break;
		}
		case OP_NULL:
		{
			push(NULL_VAL);
			break;
		}
		case OP_TRUE:
		{
			push(BOOL_VAL(true));
			break;
		}
		case OP_FALSE:
		{
			push(BOOL_VAL(false));
			break;
		}
		case OP_POP:
		{
			pop();
			break;
		}
		case OP_DUPLICATE:
		{
			push(peek(READ_BYTE()));
			break;
		}
		case OP_ASSERT_TYPE:
		{
			ObjDataType *type = AS_DATA_TYPE(READ_CONSTANT());
			if (!checkType(peek(0), *type, AS_CSTRING(READ_CONSTANT())))
				return INTERPRET_RUNTIME_ERROR;
			break;
		}
		case OP_GET_TYPE:
		{
			push(OBJ_VAL(newDataType(peek(0),false)));
			break;
		}
		case OP_TERNARY:
		{
			// stack: [..., condition, truevalue, falsevalue]
			Value falseValue = pop();
			Value trueValue = pop();
			Value condition = pop();

			push(!isFalsey(condition) ? trueValue : falseValue);
			break;
		}
		case OP_SET_NVAR:
		{
			NativeVarType var = READ_BYTE();
			// Value value = pop();
			char *name = NativeVars[var];
			switch (var)
			{
			// does nothing
			case NVAR_NULL:
				break;
			
			// sets in table
			
			// not allowed
			case NVAR_LAST:
			case NVAR_FUN:
			case NVAR_SCRIPT:
				runtimeError(formatString("Cannot set variable '%s'.", name));
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_GET_NVAR:
		{
			NativeVarType var = READ_BYTE();
			Value value;
			char *name = NativeVars[var];

			switch (var)
			{
			// gets null
			case NVAR_NULL:
				value = NULL_VAL;
				break;

			// gets depth
			case NVAR_FUN:
				value = frame->closure->function->name != NULL ?
					OBJ_VAL(frame->closure->function->name) :
					OBJ_VAL(copyString("<script>", 8));
				break;

			// gets from table
			case NVAR_LAST:
			case NVAR_SCRIPT:
				tableGet(&vm.nativeVars, copyString(name, strlen(name)), &value);
				break;
			}

			push(value);
			break;
		}
		case OP_UPDATE_LAST:
		{
			tableSet(&vm.nativeVars, copyString("_LAST", 5), peek(0));
			break;
		}
		case OP_GET_LOCAL:
		{
			uint8_t slot = READ_BYTE();
			push(frame->slots[slot]);
			break;
		}
		case OP_SET_LOCAL:
		{
			uint8_t slot = READ_BYTE();
			frame->slots[slot] = peek(0);
			break;
		}
		case OP_SET_GLOBAL:
		{
			ObjString *name = READ_STRING();
			Value type;

			// check type
			if (!tableGet(&vm.globalsTypes, name, &type))
			{
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}

			if (!checkType(peek(0), *AS_DATA_TYPE(type), "Expect value of type %s, not %s."))
				return INTERPRET_RUNTIME_ERROR;

			// set new value
			if (tableSet(&vm.globals, name, peek(0)))
			{
				runtimeError("Failed to get variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}

			break;
		}
		case OP_GET_GLOBAL:
		{
			ObjString *name = READ_STRING();
			Value value;
			if (!tableGet(&vm.globals, name, &value))
			{
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			push(value);
			break;
		}
		case OP_DEFINE_GLOBAL:
		{
			ObjString *name = READ_STRING();
			Value type = READ_CONSTANT();
			tableSet(&vm.globalsTypes, name, type);
			tableSet(&vm.globals, name, peek(0));
			pop();
			break;
		}
		case OP_GET_UPVALUE:
		{
			uint8_t slot = READ_BYTE();
			push(*frame->closure->upvalues[slot]->location);
			break;
		}
		case OP_SET_UPVALUE:
		{
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = peek(0);
			break;
		}
		case OP_DEFINE_FIELD:
		{
			ObjString *name = READ_STRING();
			Value type = READ_CONSTANT();
			tableSet(&AS_CLASS(peek(1))->fields, name, peek(0));
			tableSet(&AS_CLASS(peek(1))->fieldsTypes, name, type);
			pop();
			break;
		}
		case OP_GET_PROPERTY:
		{

			if (IS_INSTANCE(peek(0)))
			{
				ObjInstance *instance = AS_INSTANCE(peek(0));
				ObjString *name = READ_STRING();

				Value value;
				// first check for field
				if (tableGet(&instance->fields, name, &value))
				{
					pop(); // Instance.
					push(value);
					break;
				}

				// no field found so method
				if (!bindMethod(instance->klass, name))
				{
					return INTERPRET_RUNTIME_ERROR;
				}
			}

			else if (IS_MODULE(peek(0)))
			{
				ObjModule *module = AS_MODULE(peek(0));
				ObjString *name = READ_STRING();

				Value value;

				if (!tableGet(&module->fields, name, &value))
				{
					runtimeError("Undefined property '%s' of module '%s'.",
						name->chars, module->name.chars);
					return INTERPRET_RUNTIME_ERROR;
				}

				pop(); // module
				push(value);
				break;
			}

			else
			{
				Value value = peek(0);
				ObjString *name = READ_STRING();
				
				if (!bindNativeMethod(value, name))
				{
					return INTERPRET_RUNTIME_ERROR;
				}

				break;
			}

			break;
		}
		case OP_SET_PROPERTY:
		{
			if (IS_INSTANCE(peek(1)))
			{
				ObjInstance *instance = AS_INSTANCE(peek(1));
				ObjString *field = READ_STRING();
				
				// no new fields!
				if (!tableGet(&instance->fields, field, &NULL_VAL))
				{
					runtimeError("Cannot declare new field '%s' outside of class declaration.", 
						field->chars);
					return INTERPRET_RUNTIME_ERROR;
				}

				// get type
				Value type;
				if(!tableGet(&instance->fieldsTypes, field, &type))
				{
					runtimeError("Failed to get type of property '%s'.", field->chars);
					return INTERPRET_RUNTIME_ERROR;
				}

				if (!checkType(peek(0), *AS_DATA_TYPE(type), "Expected value of type %s, not %s."))
					return INTERPRET_RUNTIME_ERROR;

				tableSet(&instance->fields, field, peek(0));
				
				Value value = pop();
				pop();
				push(value);
				break;
			}
			else if (IS_MODULE(peek(1)))
			{
				ObjModule *module = AS_MODULE(peek(1));
				ObjString *field = READ_STRING();

				// no new fields!
				if (!tableGet(&module->fields, field, &NULL_VAL))
				{
					runtimeError("Cannot declare new field '%s' outside of class declaration.",
								 field->chars);
					return INTERPRET_RUNTIME_ERROR;
				}
			
				// get type
				Value type;
				if (!tableGet(&module->fieldsTypes, field, &type))
				{
					runtimeError("Failed to get type of property '%s'.", field->chars);
					return INTERPRET_RUNTIME_ERROR;
				}

				if (!checkType(peek(0), *AS_DATA_TYPE(type), "Expected value of type %s, not %s."))
					return INTERPRET_RUNTIME_ERROR;

				tableSet(&module->fields, field, peek(0));

				Value value = pop();
				pop();
				push(value);
				break;
			}
			else
			{
				runtimeError("Cannot set field of non-instance value: %s.",
							 valueToString(peek(1)));
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_GET_SUPER:
		{
			ObjString *name = READ_STRING();
			ObjClass *superclass = AS_CLASS(pop());

			if (!bindMethod(superclass, name))
			{
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_GET_INDEX:
		{
			int index = AS_NUMBER(pop());
			ObjArray *array = AS_ARRAY(pop());
			
			if (index < 0)
				// handle negative indexing
				index = array->array.count + index;

			if (index < 0 || index >= array->array.count)
			{
				runtimeError("Invalid index %d of array of length %d", index,	array->array.count);
				return INTERPRET_RUNTIME_ERROR;
			}

			// printf("%d : %d\n", array->array.count, index);
			push(array->array.values[index]);
			break;
		}
		case OP_SET_INDEX:
		{
			Value newvalue = pop();
			int index = AS_NUMBER(pop());
			ObjArray *array = AS_ARRAY(pop());

			if (index < 0)
				// handle negative indexing
				index = array->array.count + index;

			if (index < 0 || index > array->array.count)
			{
				runtimeError("Invalid index %d of array of length %d", index, array->array.count);
				return INTERPRET_RUNTIME_ERROR;
			}

			setValueArray(&array->array, index, newvalue);
			push(OBJ_VAL(array));
			break;
		}
		case OP_ARRAY_LENGTH:
		{
			Value array = pop();
			if (!IS_ARRAY(array))
			{
				runtimeError("Cannot iterate over non-array value: %s.", valueToString(array));
				return INTERPRET_RUNTIME_ERROR;
			}
			push(NUMBER_VAL(AS_ARRAY(array)->array.count));
			break;
		}
		case OP_ARRAY:
		{
			uint8_t length = READ_BYTE();
			ObjArray *array = newArray();
			
			ValueArray values;
			initValueArray(&values);
			for (int j = 0; j < length; j++)
			{
				writeValueArray(&values, pop());
			}
			// reverse items
			for (int j = length-1; j >= 0; j--)
			{
				writeValueArray(&array->array, values.values[j]);
			}
			freeValueArray(&values);
			push(OBJ_VAL(array));
			break;
		}
		case OP_EQUAL:
		{
			Value b = pop();
			Value a = pop();
			push(BOOL_VAL(valuesEqual(a, b)));
			break;
		}
		case OP_GREATER:
		{
			BINARY_OP(BOOL_VAL, >);
			break;
		}
		case OP_LESS:
		{
			BINARY_OP(BOOL_VAL, <);
			break;
		}
		case OP_ADD:
		{
			if (IS_STRING(peek(0)) && IS_STRING(peek(1)))
			{
				concatenate();
			}
			else if (IS_ARRAY(peek(0)) && IS_ARRAY(peek(1)))
			{
				ObjArray *b = AS_ARRAY(pop());
				ObjArray *a = AS_ARRAY(pop());
				for (int i = 0; i < b->array.count; i++)
					writeValueArray(&a->array, b->array.values[i]);
				freeValueArray(&b->array);
				push(OBJ_VAL(a));
			}
			else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1)))
			{
				double b = AS_NUMBER(pop());
				double a = AS_NUMBER(pop());
				push(NUMBER_VAL(a + b));
			}
			else
			{
				runtimeError("Operands must be two numbers or two strings.");
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_INCREMENT:
		{
			if(!IS_NUMBER(peek(0)))
			{
				runtimeError("Cannot increment non-numerical value '%s'.", valueToString(peek(0)));
				return INTERPRET_RUNTIME_ERROR;
			}
			push(NUMBER_VAL(AS_NUMBER(pop())+1));
			break;
		}
		case OP_SUBTRACT:
		{
			BINARY_OP(NUMBER_VAL, -);
			break;
		}
		case OP_DECREMENT:
		{
			if (!IS_NUMBER(peek(0)))
			{
				runtimeError("Cannot decrement non-numerical value '%s'.", valueToString(peek(0)));
				return INTERPRET_RUNTIME_ERROR;
			}
			push(NUMBER_VAL(AS_NUMBER(pop()) - 1));
			break;
		}
		case OP_MULTIPLY:
		{
			BINARY_OP(NUMBER_VAL, *);
			break;
		}
		case OP_DIVIDE:
		{
			BINARY_OP(NUMBER_VAL, /);
			break;
		}
		case OP_MODULO:
		{
			Value b = pop();
			Value a = pop();
			if (!(IS_NUMBER(a) && IS_NUMBER(b)))
			{
				runtimeError("Operands must be numbers");
				return INTERPRET_RUNTIME_ERROR;
			}
			push(NUMBER_VAL(fmod(AS_NUMBER(a), AS_NUMBER(b))));
			break;
		}
		case OP_NOT:
		{
			push(BOOL_VAL(isFalsey(pop())));
			break;
		}
		case OP_NEGATE:
		{
			if (!IS_NUMBER(peek(0)))
			{
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			push(NUMBER_VAL(-AS_NUMBER(pop())));
			break;
		}
		case OP_PRINT:
		{
			printValue(pop());
			break;
		}
		case OP_PRINT_LN:
		{
			printValue(pop());
			#ifndef DEBUG_TRACE_EXECUTION
				printf("\n");
			#endif
			break;
		}
		case OP_JUMP:
		{
			uint16_t offset = READ_SHORT();
			frame->ip += offset;
			break;
		}
		case OP_JUMP_IF_FALSE:
		{
			uint16_t offset = READ_SHORT();
			if (isFalsey(peek(0)))
				frame->ip += offset;
			break;
		}
		case OP_JUMP_BACK:
		{
			uint16_t offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}
		case OP_CALL:
		{
			int argCount = READ_BYTE();
			if (!callValue(peek(argCount), argCount))
			{
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}
		case OP_INVOKE:
		{
			ObjString *method = READ_STRING();
			int argCount = READ_BYTE();
			if (!invoke(method, argCount))
			{
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}
		case OP_SUPER_INVOKE:
		{
			ObjString *method = READ_STRING();
			int argCount = READ_BYTE();
			ObjClass *superclass = AS_CLASS(pop());
			if (!invokeFromClass(superclass, method, argCount))
			{
				return INTERPRET_RUNTIME_ERROR;
			}
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}
		case OP_CLOSURE:
		{
			ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
			ObjClosure *closure = newClosure(function);
			push(OBJ_VAL(closure));
			// catch upvalues
			for (int i = 0; i < closure->upvalueCount; i++)
			{
				uint8_t isLocal = READ_BYTE();
				uint8_t index = READ_BYTE();
				if (isLocal)
				{
					closure->upvalues[i] =
						captureUpvalue(frame->slots + index);
				}
				else
				{
					closure->upvalues[i] = frame->closure->upvalues[index];
				}
			}
			break;
		}
		case OP_CLOSE_UPVALUE:
		{
			closeUpvalues(vm.stackTop - 1);
			pop();
			break;
		}
		case OP_CLASS:
		{
			push(OBJ_VAL(newClass(READ_STRING())));
			break;
		}
		case OP_INHERIT:
		{
			Value superclass = peek(1);

			if (!IS_CLASS(superclass))
			{
				runtimeError("Superclass must be a class.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjClass *subclass = AS_CLASS(peek(0));
			tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
			tableAddAll(&AS_CLASS(superclass)->fields, &subclass->fields);
			tableAddAll(&AS_CLASS(superclass)->fieldsTypes, &subclass->fieldsTypes);
			pop(); // Subclass.
			break;
		}
		case OP_METHOD:
		{
			defineMethod(READ_STRING());
			break;
		}
		case OP_IMPORT:
		{
			char *name = READ_STRING()->chars;
			char *basename = formatString("%s.brc", name);
			char *filepath = "";
			FILE *file;
			
			// check current directory
			{
				// get dir of script being ran
				Value snameval; 
				tableGet(&vm.nativeVars, copyString("_SCRIPT", 7), &snameval);
				
				size_t dlength;
				cwk_path_get_dirname(AS_CSTRING(snameval), &dlength);
				char *sdir = formatString("%.*s", dlength, AS_CSTRING(snameval));

				// join dir and basename
				size_t fnamesize = cwk_path_join(sdir, basename, NULL, 0);
				char fname[fnamesize];
				cwk_path_join(sdir, basename, fname, fnamesize + 1);
				
				filepath = fname;
				file = fopen(fname, "rb");
			}

			// check in lib path
			if (file == NULL)
			{
				size_t dlength = cwk_path_join(BRACE_LIB_PATH, basename, NULL, 0);
				char fname[dlength];
				cwk_path_join(BRACE_LIB_PATH, basename, fname, dlength + 1);
				
				filepath = fname;
				file = fopen(fname, "rb");
			}

			// else // not found
			if (file == NULL)
			{
				runtimeError("Error importing from '%s': file '%s' not found.",
					name, formatString("%s.brc", name));
				return INTERPRET_RUNTIME_ERROR;
			}

			// read source
			char *source = "";
			{
				fseek(file, 0L, SEEK_END);
				size_t fileSize = ftell(file);
				rewind(file);

				char *buffer = (char *)malloc(fileSize + 1);
				if (buffer == NULL)
				{
					fprintf(stderr, "Not enough memory to read \"%s\".\n", basename);
					exit(74);
				}
				size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
				if (bytesRead < fileSize)
				{
					fprintf(stderr, "Could not read file \"%s\".\n", basename);
					exit(74);
				}
				buffer[bytesRead] = '\0';

				fclose(file);
				source = buffer;
			}

			// backup
			VM oldVm = vm;

			// interpret module
			initVM(true);
			tableAddAll(&oldVm.strings, &vm.strings); // keep hashed strings

			#ifdef DEBUG_TRACE_EXECUTION
			printf("\n\n======== MODULE '%s' ========\n\n", name);
			#endif
			InterpretResult result = interpret(filepath, source, true);
			#ifdef DEBUG_TRACE_EXECUTION
			printf("\n\n======== ================ ========\n\n");
			#endif
			free(source);

			// check result
			if (result != INTERPRET_OK)
				return result;

			// copy globals over to new module
			ObjModule *module = newModule(name, filepath);
			tableAddAll(&vm.globals, &module->fields);
			tableAddAll(&vm.globalsTypes, &module->fieldsTypes);

			// restore old vm
			vm = oldVm;

			push(OBJ_VAL(module));
			break;
		}
		case OP_RETURN:
		{
			Value result = pop();

			if (!checkType(result, frame->closure->function->returnType,
					"Expected return type %s, not %s."))
				return INTERPRET_RUNTIME_ERROR;

			closeUpvalues(frame->slots);
			vm.frameCount--;
			if (vm.frameCount == 0)
			{
				pop();
				return INTERPRET_OK;
			}

			vm.stackTop = frame->slots;
			push(result);
			frame = &vm.frames[vm.frameCount - 1];
			break;
		}
		case OP_EXIT:
		{
			Value retval = pop();
			if (!IS_NUMBER(retval))
			{
				runtimeError("cannot exit from script with non-number value");
				return INTERPRET_RUNTIME_ERROR;
			}

			#ifdef DEBUG_TRACE_EXECUTION
				printf("\nExited with code %d.\n", (int)AS_NUMBER(retval));
			#endif
			exit((int)AS_NUMBER(retval));
			break;
		}
		case OP_SCRIPT_END:
		{
			if (repl_mode)
			{
				Value result;
				tableGet(&vm.nativeVars, copyString("_LAST", 5), &result);
				printf("\n(_LAST = ");
				printValue(result);
				printf(")\n");
				return INTERPRET_OK;
			}
			else
				return INTERPRET_OK;
		}
		}
	}

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
}

// interpret shit and return its result
InterpretResult interpret(const char *path, const char *source, bool repl_mode)
{
	tableSet(&vm.nativeVars, copyString("_SCRIPT", 7), 
		OBJ_VAL(copyString(path, strlen(path))));
	
	ObjFunction *function = compile(source);
	if (function == NULL)
		return INTERPRET_COMPILE_ERROR;

	push(OBJ_VAL(function));
	ObjClosure *closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	call(closure, 0);
	InterpretResult result = run(repl_mode);
	#ifdef DEBUG_TRACE_EXECUTION
		printf("\n");
	#endif
	return result;
}
