#include "node.h"
using namespace v8;
using namespace node;

class WrappedContext {
private:
    Persistent<Context> context;

    WrappedContext(Persistent<Context> ctx) { context = ctx; }

    ~WrappedContext() { context.Dispose(); }

public:
    // Create a new WrappedContext and set it as a hidden property in the
    // sandbox and in the context's global object.
    static void Embed(Local<Object> sbx, Persistent<Context> context) {
        HandleScope scope;
        Persistent<Object> sandbox = Persistent<Object>::New(sbx);
        WrappedContext* wc = new WrappedContext(context);

        Local<ObjectTemplate> ctxTmpl = ObjectTemplate::New();
        ctxTmpl->SetInternalFieldCount(1);
        Local<Object> wrapped = ctxTmpl->NewInstance();
        wrapped->SetPointerInInternalField(0, wc);

        sandbox->SetHiddenValue(String::New("context"), wrapped);
        context->Global()->SetHiddenValue(String::New("context"), wrapped);
        sandbox.MakeWeak((void*) wc, WrappedContext::CleanupContext);
    }

    static Persistent<Context> Extract(Local<Object> sandbox) {
        HandleScope scope;
        Local<Object> wrappedObj =
            sandbox->GetHiddenValue(String::New("context"))->ToObject();
        WrappedContext* wrappedCtx =
            (WrappedContext*) wrappedObj->GetPointerFromInternalField(0);
        return wrappedCtx->context;
    }

    static void CleanupContext (Persistent<Value> sandbox, void* parameter) {
        WrappedContext* wc = (WrappedContext*) parameter;
        delete wc;
        sandbox.Dispose();
    }
};

static Handle<Value> Wrap(const Arguments& args);
static Handle<Value> Run(const Arguments& args);
static Persistent<Context> CreateContext(Handle<Object> sandbox);
static Handle<Value> GetGlobal(const Arguments& args);

static Handle<Value> GlobalPropertyGetter(Local<String> property,
                                          const AccessorInfo &info);
static Handle<Value> GlobalPropertySetter(Local<String> property,
                                          Local<Value> value,
                                          const AccessorInfo &info);
static Handle<Integer> GlobalPropertyQuery(Local<String> property,
                                           const AccessorInfo &info);
static Handle<Boolean> GlobalPropertyDeleter(Local<String> property,
                                             const AccessorInfo &info);
static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &info);

// args[0] = the sandbox object
static Handle<Value> Wrap(const Arguments& args) {
    HandleScope scope;
    Local<Object> sandbox;
    if ((args.Length() > 0) && (args[0]->IsObject())) {
        sandbox = args[0]->ToObject();
    } else {
        sandbox = Object::New();
    }
    Persistent<Context> context = CreateContext(sandbox);
    WrappedContext::Embed(sandbox, context);
    NODE_SET_METHOD(sandbox, "run", Run);
    NODE_SET_METHOD(sandbox, "getGlobal", GetGlobal);
    return scope.Close(sandbox);
}

// Create a context whose global object uses the sandbox to lookup and set
// properties.
static Persistent<Context> CreateContext(Handle<Object> sandbox) {
    HandleScope scope;
    // Set up the context's global object.
    Local<FunctionTemplate> ftmpl = FunctionTemplate::New();
    ftmpl->SetHiddenPrototype(true);
    ftmpl->SetClassName(sandbox->GetConstructorName());
    Local<ObjectTemplate> otmpl = ftmpl->InstanceTemplate();
    otmpl->SetNamedPropertyHandler(GlobalPropertyGetter,
                                   GlobalPropertySetter,
                                   GlobalPropertyQuery,
                                   GlobalPropertyDeleter,
                                   GlobalPropertyEnumerator,
                                   sandbox);
    Persistent<Context> context = Context::New(NULL, otmpl);
    // Get rid of the proxy object.
    context->DetachGlobal();
    return context;
}

/*
 * args[0] = String of code
 * args[1] = filename
 */
static Handle<Value> Run(const Arguments& args) {
    HandleScope scope;
    Local<Object> sandbox = args.This();
    Persistent<Context> context = WrappedContext::Extract(sandbox);
    context->Enter();
    Local<String> code = args[0]->ToString();
    TryCatch trycatch;
    Handle<Script> script;
    if (args.Length() > 1) {
        script = Script::Compile(code, args[1]->ToString());
    } else {
        script = Script::Compile(code);
    }
    if (script.IsEmpty()) {
      return trycatch.ReThrow();
    }
    Handle<Value> result = script->Run();
    if (result.IsEmpty()) {
        return trycatch.ReThrow();
    }
    context->Exit();
}

static Handle<Value> GetGlobal(const Arguments& args) {
    HandleScope scope;
    Local<Object> sandbox = args.This();
    Persistent<Context> context = WrappedContext::Extract(sandbox);
    return scope.Close(context->Global());
}

static Handle<Value> GlobalPropertyGetter (Local<String> property,
                                           const AccessorInfo &info) {
    HandleScope scope;
    Local<Object> sandbox = info.Data()->ToObject();
    // First check the sandbox object.
    Local<Value> rv = sandbox->Get(property);
    if (rv->IsUndefined()) {
        // Next, check the global object (things like Object, Array, etc).
        Persistent<Context> context = WrappedContext::Extract(sandbox);
        // This needs to call GetRealNamedProperty or else we'll get stuck in
        // an infinite loop here.
        rv = context->Global()->GetRealNamedProperty(property);
    }
    return scope.Close(rv);
}

// Global variables get set back on the window object.
static Handle<Value> GlobalPropertySetter (Local<String> property,
                                           Local<Value> value,
                                           const AccessorInfo &info) {
    HandleScope scope;
    Local<Object> sandbox = info.Data()->ToObject();
    sandbox->Set(property, value);
    return scope.Close(value);
}

static Handle<Integer> GlobalPropertyQuery(Local<String> property,
                                           const AccessorInfo &info) {
    HandleScope scope;
    return scope.Close(Integer::New(None));
}

static Handle<Boolean> GlobalPropertyDeleter(Local<String> property,
                                             const AccessorInfo &info) {
    HandleScope scope;
    Local<Object> sandbox = info.Data()->ToObject();
    bool success = sandbox->Delete(property);
    if (!success) {
        Persistent<Context> context = WrappedContext::Extract(sandbox);
        success = context->Global()->Delete(property);
    }
    return scope.Close(Boolean::New(success));
}

static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &info) {
    HandleScope scope;
    Local<Object> sandbox = info.Data()->ToObject();
    return scope.Close(sandbox->GetPropertyNames());
}

// Export the C++ Wrap method as 'wrap' on the module.
static void Init(Handle<Object> target) {
    HandleScope scope;
    NODE_SET_METHOD(target, "wrap", Wrap);
}

extern "C" {
  static void init(Handle<Object> target) {
    Init(target);
  }
  NODE_MODULE(contextify, init);
}
