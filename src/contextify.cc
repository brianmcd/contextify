#include "node.h"
#include <string>
using namespace v8;
using namespace node;

struct ContextifyInfo;

static Handle<Value> Wrap(const Arguments& args);
static Handle<Value> Run(const Arguments& args);
static Handle<Value> GetGlobal(const Arguments& args);
static Persistent<Context> CreateContext(ContextifyInfo* info);

// Interceptor functions for global object template.
static Handle<Value> GlobalPropertyGetter(Local<String> property,
                                          const AccessorInfo &accessInfo);
static Handle<Value> GlobalPropertySetter(Local<String> property,
                                          Local<Value> value,
                                          const AccessorInfo &accessInfo);
static Handle<Integer> GlobalPropertyQuery(Local<String> property,
                                           const AccessorInfo &accessInfo);
static Handle<Boolean> GlobalPropertyDeleter(Local<String> property,
                                             const AccessorInfo &accessInfo);
static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &accessInfo);

// The lifetime of ContextifyInfo is bound to that of the sandbox.
struct ContextifyInfo {
    Persistent<Context> context;
    Persistent<Object> sandbox;

    void SetContext(Persistent<Context> context) {
        this->context = context;
    }

    void SetSandbox(Local<Object> sandbox) {
        this->sandbox = Persistent<Object>::New(sandbox);
        // When the sandbox is ready to be GC'd, we want to delete ourselves.
        this->sandbox.MakeWeak(this, ContextifyInfo::Cleanup);
    }

    // This is triggered when the Sandbox is ready to be GC'd.  In that case,
    // we want to get rid of the context, the sandbox, and our references.
    static void Cleanup(Persistent<Value> value, void* param) {
        ContextifyInfo* info = static_cast<ContextifyInfo*>(param);
        info->context.Dispose();
        info->context.Clear();
        info->sandbox.Dispose();
        info->sandbox.Clear();
        value.Dispose();
        value.Clear();
        delete info;
    }
};

// We only want to create 1 Function instance for each of these in this
// context.  Was previously creating a new Function for each Contextified
// object and causing a memory leak.
Persistent<Function> runFunc;
Persistent<Function> getGlobalFunc;

// args[0] = the sandbox object
static Handle<Value> Wrap(const Arguments& args) {
    HandleScope scope;
    Local<Object> sandbox;
    if ((args.Length() > 0) && (args[0]->IsObject())) {
        sandbox = args[0]->ToObject();
    } else {
        sandbox = Object::New();
    }
    // info is cleaned up by itself when the sandbox gets GC'd.
    ContextifyInfo* info = new ContextifyInfo();
    info->SetSandbox(sandbox);
    Persistent<Context> context = CreateContext(info);
    info->SetContext(context);

    Local<Value> wrapped = External::Wrap(info);
    sandbox->SetHiddenValue(String::New("info"), wrapped);
    context->Global()->SetHiddenValue(String::New("info"), wrapped);

    if (runFunc.IsEmpty()) {
        Local<FunctionTemplate> runTmpl = FunctionTemplate::New(Run);
        runFunc = Persistent<Function>::New(runTmpl->GetFunction());
    }
    sandbox->Set(String::NewSymbol("run"), runFunc);

    if (getGlobalFunc.IsEmpty()) {
        Local<FunctionTemplate> getGlobalTmpl = FunctionTemplate::New(GetGlobal);
        getGlobalFunc = Persistent<Function>::New(getGlobalTmpl->GetFunction());
    }
    sandbox->Set(String::NewSymbol("getGlobal"), getGlobalFunc);

    return scope.Close(sandbox);
}

// Create a context whose global object uses the sandbox to lookup and set
// properties.
static Persistent<Context> CreateContext(ContextifyInfo* info) {
    HandleScope scope;
    // Set up the context's global object.
    Local<FunctionTemplate> ftmpl = FunctionTemplate::New();
    ftmpl->SetHiddenPrototype(true);
    ftmpl->SetClassName(info->sandbox->GetConstructorName());
    Local<ObjectTemplate> otmpl = ftmpl->InstanceTemplate();
    otmpl->SetNamedPropertyHandler(GlobalPropertyGetter,
                                   GlobalPropertySetter,
                                   GlobalPropertyQuery,
                                   GlobalPropertyDeleter,
                                   GlobalPropertyEnumerator,
                                   External::Wrap(info));
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
    Local<Value> wrapped = args.This()->GetHiddenValue(String::New("info"));
    void* unwrapped = External::Unwrap(wrapped);
    ContextifyInfo* info = static_cast<ContextifyInfo*>(unwrapped);
    Persistent<Context> context = info->context;
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
      context->Exit();
      return trycatch.ReThrow();
    }
    Handle<Value> result = script->Run();
    context->Exit();
    if (result.IsEmpty()) {
        return trycatch.ReThrow();
    }
    return scope.Close(result);
}

static Handle<Value> GetGlobal(const Arguments& args) {
    HandleScope scope;
    Local<Value> wrapped = args.This()->GetHiddenValue(String::New("info"));
    void* unwrapped = External::Unwrap(wrapped);
    ContextifyInfo* info = static_cast<ContextifyInfo*>(unwrapped);
    Persistent<Context> context = info->context;
    return scope.Close(context->Global());
}

static Handle<Value> GlobalPropertyGetter (Local<String> property,
                                           const AccessorInfo &accessInfo) {
    HandleScope scope;
    void* unwrapped = External::Unwrap(accessInfo.Data());
    ContextifyInfo* info = static_cast<ContextifyInfo*>(unwrapped);
    Persistent<Object> sandbox = info->sandbox;
    // First check the sandbox object.
    Local<Value> rv = sandbox->Get(property);
    if (rv->IsUndefined()) {
        // Next, check the global object (things like Object, Array, etc).
        Persistent<Context> context = info->context;
        // This needs to call GetRealNamedProperty or else we'll get stuck in
        // an infinite loop here.
        rv = context->Global()->GetRealNamedProperty(property);
    }
    return scope.Close(rv);
}

// Global variables get set back on the sandbox object.
static Handle<Value> GlobalPropertySetter (Local<String> property,
                                           Local<Value> value,
                                           const AccessorInfo &accessInfo) {
    HandleScope scope;
    void* unwrapped = External::Unwrap(accessInfo.Data());
    ContextifyInfo* info = static_cast<ContextifyInfo*>(unwrapped);
    Persistent<Object> sandbox = info->sandbox;
    bool success = sandbox->Set(property, value);
    return scope.Close(value);
}

static Handle<Integer> GlobalPropertyQuery(Local<String> property,
                                           const AccessorInfo &accessInfo) {
    HandleScope scope;
    return scope.Close(Integer::New(None));
}

static Handle<Boolean> GlobalPropertyDeleter(Local<String> property,
                                             const AccessorInfo &accessInfo) {
    HandleScope scope;
    void* unwrapped = External::Unwrap(accessInfo.Data());
    ContextifyInfo* info = static_cast<ContextifyInfo*>(unwrapped);
    Persistent<Object> sandbox = info->sandbox;
    bool success = sandbox->Delete(property);
    if (!success) {
        Persistent<Context> context = info->context;
        success = context->Global()->Delete(property);
    }
    return scope.Close(Boolean::New(success));
}

static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &accessInfo) {
    HandleScope scope;
    void* unwrapped = External::Unwrap(accessInfo.Data());
    ContextifyInfo* info = static_cast<ContextifyInfo*>(unwrapped);
    Persistent<Object> sandbox = info->sandbox;
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
