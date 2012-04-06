#include "node.h"
#include <string>
using namespace v8;
using namespace node;

// For some reason this needs to be out of the object or node won't load the
// library.
static Persistent<FunctionTemplate> dataWrapperTmpl;
static Persistent<Function>         dataWrapperCtor;

class ContextifyContext : ObjectWrap {
public:
    Persistent<Context> context;
    Persistent<Object>  sandbox;
    Persistent<Object>  proxyGlobal;

    static Persistent<FunctionTemplate> jsTmpl;

    ContextifyContext(Local<Object> sbox) {
        HandleScope scope;
        sandbox = Persistent<Object>::New(sbox);
    }

    ~ContextifyContext() {
        context.Dispose();
        context.Clear();
        proxyGlobal.Dispose();
        proxyGlobal.Clear();
        sandbox.Dispose();
        sandbox.Clear();
    }

    // We override ObjectWrap::Wrap so that we can create our context after
    // we have a reference to our "host" JavaScript object.  If we try to use
    // handle_ in the ContextifyContext constructor, it will be empty since it's
    // set in ObjectWrap::Wrap.
    inline void Wrap(Handle<Object> handle) {
        ObjectWrap::Wrap(handle);
        context     = createV8Context();
        proxyGlobal = Persistent<Object>::New(context->Global());
    }
    
    // This is an object that just keeps an internal pointer to this
    // ContextifyContext.  It's passed to the NamedPropertyHandler.  If we
    // pass the main JavaScript context object we're embedded in, then the
    // NamedPropertyHandler will store a reference to it forever and keep it
    // from getting gc'd.
    Local<Object> createDataWrapper () {
        HandleScope scope;
        Local<Object> wrapper = dataWrapperCtor->NewInstance();
        wrapper->SetPointerInInternalField(0, this);
        return scope.Close(wrapper);
    }

    Persistent<Context> createV8Context() {
        HandleScope scope;
        Local<FunctionTemplate> ftmpl = FunctionTemplate::New();
        ftmpl->SetHiddenPrototype(true);
        ftmpl->SetClassName(sandbox->GetConstructorName());
        Local<ObjectTemplate> otmpl = ftmpl->InstanceTemplate();
        otmpl->SetNamedPropertyHandler(GlobalPropertyGetter,
                                       GlobalPropertySetter,
                                       GlobalPropertyQuery,
                                       GlobalPropertyDeleter,
                                       GlobalPropertyEnumerator,
                                       createDataWrapper());
        otmpl->SetAccessCheckCallbacks(GlobalPropertyNamedAccessCheck,
                                       GlobalPropertyIndexedAccessCheck);
        return Context::New(NULL, otmpl);
    }

    static void Init(Handle<Object> target) {
        HandleScope scope;
        dataWrapperTmpl = Persistent<FunctionTemplate>::New(FunctionTemplate::New());
        dataWrapperTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        dataWrapperCtor = Persistent<Function>::New(dataWrapperTmpl->GetFunction());

        jsTmpl = Persistent<FunctionTemplate>::New(FunctionTemplate::New(New));
        jsTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        jsTmpl->SetClassName(String::NewSymbol("ContextifyContext"));

        NODE_SET_PROTOTYPE_METHOD(jsTmpl, "run",       ContextifyContext::Run);
        NODE_SET_PROTOTYPE_METHOD(jsTmpl, "getGlobal", ContextifyContext::GetGlobal);

        target->Set(String::NewSymbol("ContextifyContext"), jsTmpl->GetFunction());
    }

    // args[0] = the sandbox object
    static Handle<Value> New(const Arguments& args) {
        HandleScope scope;
        if (args.Length() < 1) {
            Local<String> msg = String::New("Wrong number of arguments passed to ContextifyContext constructor");
            return ThrowException(Exception::Error(msg));
        }
        if (!args[0]->IsSpecObject()) {
            Local<String> msg = String::New("Argument to ContextifyContext constructor must be an object.");
            return ThrowException(Exception::Error(msg));
        }
        ContextifyContext* ctx = new ContextifyContext(args[0]->ToObject());
        ctx->Wrap(args.This());
        return args.This();
    }

    static Handle<Value> Run(const Arguments& args) {
        HandleScope scope;
        if (args.Length() == 0) {
            Local<String> msg = String::New("Must supply at least 1 argument to run");
            return ThrowException(Exception::Error(msg));
        }
        if (!args[0]->IsString()) {
            Local<String> msg = String::New("First argument to run must be a String.");
            return ThrowException(Exception::Error(msg));
        }
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args.This());
        Persistent<Context> context = ctx->context;
        context->Enter();
        Local<String> code = args[0]->ToString();
        TryCatch trycatch;
        Handle<Script> script;
        if (args.Length() > 1 && args[1]->IsString()) {
            script = Script::Compile(code, args[1]->ToString());
        } else {
            script = Script::Compile(code);
        }
        if (script.IsEmpty()) {
          context->Exit();
          PrintException(trycatch);
          return trycatch.ReThrow();
        }
        Handle<Value> result = script->Run();
        context->Exit();
        if (result.IsEmpty()) {
            PrintException(trycatch);
            return trycatch.ReThrow();
        }
        return scope.Close(result);
    }

    static Handle<Value> GetGlobal(const Arguments& args) {
        HandleScope scope;
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args.This());
        return ctx->proxyGlobal;
    }

    static bool GlobalPropertyNamedAccessCheck(Local<Object> host,
                                               Local<Value>  key,
                                               AccessType    type,
                                               Local<Value>  data) {
        return true;
    }

    static bool GlobalPropertyIndexedAccessCheck(Local<Object> host,
                                                 uint32_t      key,
                                                 AccessType    type,
                                                 Local<Value>  data) {
        return true;
    }

    static Handle<Value> GlobalPropertyGetter (Local<String> property,
                                               const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        Local<Value> rv = ctx->sandbox->Get(property);
        if (rv.IsEmpty()) {
            rv = ctx->proxyGlobal->Get(property);
        }
        return scope.Close(rv);
    }

    static Handle<Value> GlobalPropertySetter (Local<String> property,
                                               Local<Value> value,
                                               const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        ctx->sandbox->Set(property, value);
        return scope.Close(value);
    }

    static Handle<Integer> GlobalPropertyQuery(Local<String> property,
                                               const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        if (!ctx->sandbox->Has(property) ||
            !ctx->proxyGlobal->Has(property)) {
            return scope.Close(Integer::New(None));
        }
        return scope.Close(Handle<Integer>());
    }

    static Handle<Boolean> GlobalPropertyDeleter(Local<String> property,
                                                 const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        bool success = ctx->sandbox->Delete(property);
        if (!success) {
            success = ctx->proxyGlobal->Delete(property);
        }
        return scope.Close(Boolean::New(success));
    }

    static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        return scope.Close(ctx->sandbox->GetPropertyNames());
    }

    // Mostly taken from d8 shell.
    static void PrintException(TryCatch &try_catch) {
        HandleScope handle_scope;
        String::Utf8Value exception(try_catch.Exception());
        const char* exception_string = ToCString(exception);
        Handle<v8::Message> message = try_catch.Message();
        if (message.IsEmpty()) {
            // V8 didn't provide any extra information about this error; just
            // print the exception.
            fprintf(stderr, "%s\n", exception_string);
        } else {
            // Print (filename):(line number): (message).
            String::Utf8Value filename(message->GetScriptResourceName());
            const char* filename_string = ToCString(filename);
            int linenum = message->GetLineNumber();
            fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
            // Print line of source code.
            String::Utf8Value sourceline(message->GetSourceLine());
            const char* sourceline_string = ToCString(sourceline);
            fprintf(stderr, "%s\n", sourceline_string);
            // Print wavy underline (GetUnderline is deprecated).
            int start = message->GetStartColumn();
            for (int i = 0; i < start; i++) {
                fprintf(stderr, " ");
            }
            int end = message->GetEndColumn();
            for (int i = start; i < end; i++) {
                fprintf(stderr, "^");
            }
            fprintf(stderr, "\n");
            String::Utf8Value stack_trace(try_catch.StackTrace());
            if (stack_trace.length() > 0) {
                const char* stack_trace_string = ToCString(stack_trace);
                fprintf(stderr, "%s\n", stack_trace_string);
            }
        }
    }

    // Extracts a C string from a V8 Utf8Value.
    static const char* ToCString(const String::Utf8Value& value) {
        return *value ? *value : "<string conversion failed>";
    }
};

Persistent<FunctionTemplate> ContextifyContext::jsTmpl;

extern "C" {
    static void init(Handle<Object> target) {
        ContextifyContext::Init(target);
    }
    NODE_MODULE(contextify, init);
};
