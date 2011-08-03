#include "node.h"
#include <string>
using namespace v8;
using namespace node;

// This class exists so we can store hidden properties that aren't values
// (e.g. we need to store a Context inside of the sandbox and global objects).
// SetHiddenValue only accepts a Value.
template <class T>
class WrappedHandle {
public:
    Persistent<T> m_handle;
    std::string m_tag;
    WrappedHandle(Handle<T> handle, std::string tag = "", bool weak = false) {
        m_handle = Persistent<T>::New(handle);
        m_tag = tag;
        if (weak) {
            m_handle.MakeWeak(NULL, WrappedHandle::WeakCleanup);
        }
    };

    static void WeakCleanup(Persistent<Value> value, void* param) {
        value.Dispose();
        value.Clear();
    }
};

static Handle<Value> Wrap(const Arguments& args);
static Handle<Value> Run(const Arguments& args);
static Persistent<Context> CreateContext(Local<Object> sandbox);
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

static void CleanupSandbox(Persistent<Value> sandbox, void* param) {
    static int count = 0;
    HandleScope scope;
    WrappedHandle<Context>* wh = (WrappedHandle<Context>*) param;
    wh->m_handle.Dispose(); // Free the context.
    wh->m_handle.Clear();
    delete wh; // Free the WrappedHandle itself.
    sandbox.Dispose(); // Free the sandbox.
    sandbox.Clear();
};

static void CleanupContext(Persistent<Value> context, void* param) {
    context.Dispose();
    context.Clear();
}

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
    WrappedHandle<Context>* wh = new WrappedHandle<Context>(context, "context");
    context.MakeWeak(NULL, CleanupContext);
    Local<Value> external = External::Wrap(wh);
    sandbox->SetHiddenValue(String::New("wrapped"), external);
    context->Global()->SetHiddenValue(String::New("wrapped"), external);
    NODE_SET_METHOD(sandbox, "run", Run);
    NODE_SET_METHOD(sandbox, "getGlobal", GetGlobal);

    Persistent<Object> weakSandbox = Persistent<Object>::New(sandbox);
    weakSandbox.MakeWeak((void*) wh, CleanupSandbox);
    return scope.Close(sandbox);
}

static void CleanupSandboxNamedRef(Persistent<Value> sandbox, void* param) {
    HandleScope scope;
    WrappedHandle<Object>* wh = (WrappedHandle<Object>*) param;
    delete wh;
    sandbox.Dispose();
    sandbox.Clear();
}

// Create a context whose global object uses the sandbox to lookup and set
// properties.
static Persistent<Context> CreateContext(Local<Object> sandbox) {
    static int count = 0;
    //printf("Created: %d\n", ++count);
    HandleScope scope;

    // We can't pass the sandbox directly to SetNamedPropertyHandler, or else
    // that will keep it alive forever (since the global won't be GC'd until
    // the context is destroyed, but the context won't get destroyed until the
    // the sandbox gets GC'd).
    // 
    // So, we embed a WrappedHandle into a fresh object as an internal field.
    WrappedHandle<Object>* wh = new WrappedHandle<Object>(sandbox, "sandbox", true);
    Local<ObjectTemplate> wrapperTmpl = ObjectTemplate::New();
    wrapperTmpl->SetInternalFieldCount(1);
    Local<Object> wrapped = wrapperTmpl->NewInstance();
    wrapped->SetPointerInInternalField(0, wh);

    // So we can clean up the WrappedHandle:
    Persistent<Object> weakSandbox = Persistent<Object>::New(sandbox);
    weakSandbox.MakeWeak((void*) wh, CleanupSandboxNamedRef);

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
                                   wrapped);
    Persistent<Context> context = Context::New(NULL, otmpl);
    // Get rid of the proxy object.
    context->DetachGlobal();
    return context;
}

static Persistent<Context> UnwrapContext(Handle<Object> target) {
    HandleScope scope;
    Local<Value> external = target->GetHiddenValue(String::New("wrapped"));
    WrappedHandle<Context>* wh =
        (WrappedHandle<Context>*) External::Unwrap(external);
    return wh->m_handle;
}

/*
 * args[0] = String of code
 * args[1] = filename
 */
static Handle<Value> Run(const Arguments& args) {
    HandleScope scope;
    Persistent<Context> context = UnwrapContext(args.This());
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
    Persistent<Context> context = UnwrapContext(args.This());
    return scope.Close(context->Global());
}

static Persistent<Object> UnwrapSandbox(Local<Object> target) {
    HandleScope scope;
    WrappedHandle<Object>* wh =
        (WrappedHandle<Object>*) target->GetPointerFromInternalField(0);
    return wh->m_handle;
}

static Handle<Value> GlobalPropertyGetter (Local<String> property,
                                           const AccessorInfo &info) {
    HandleScope scope;
    Persistent<Object> sandbox = UnwrapSandbox(info.Data()->ToObject());
    // First check the sandbox object.
    Local<Value> rv = sandbox->Get(property);
    if (rv->IsUndefined()) {
        // Next, check the global object (things like Object, Array, etc).
        Persistent<Context> context = UnwrapContext(sandbox);
        // This needs to call GetRealNamedProperty or else we'll get stuck in
        // an infinite loop here.
        rv = context->Global()->GetRealNamedProperty(property);
    }
    return scope.Close(rv);
}

// Global variables get set back on the sandbox object.
static Handle<Value> GlobalPropertySetter (Local<String> property,
                                           Local<Value> value,
                                           const AccessorInfo &info) {
    HandleScope scope;
    Persistent<Object> sandbox = UnwrapSandbox(info.Data()->ToObject());
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
    Persistent<Object> sandbox = UnwrapSandbox(info.Data()->ToObject());
    bool success = sandbox->Delete(property);
    if (!success) {
        Persistent<Context> context = UnwrapContext(sandbox);
        success = context->Global()->Delete(property);
    }
    return scope.Close(Boolean::New(success));
}

static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &info) {
    HandleScope scope;
    Persistent<Object> sandbox = UnwrapSandbox(info.Data()->ToObject());
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
