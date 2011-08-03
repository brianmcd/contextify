#include "node.h"
#include <string>
using namespace v8;
using namespace node;

template <class T>
class WrappedRef {
public:
    Persistent<T> m_handle;
    bool m_weak;
    std::string m_tag;

    // TODO: it would be cleaner to take a local and make a new ref here, according to weak.
    WrappedRef(Persistent<T> handle, bool weak, std::string tag) : m_weak(weak) {
        m_handle = handle;
        m_tag = tag;
        if (weak) {
            m_handle.MakeWeak(this, WrappedRef<T>::FreeHandle);
        }
    }

    ~WrappedRef() {
        if (!m_weak) {
            m_handle.Dispose();
        }
    }

    static void FreeHandle(Persistent<Value> handle, void* param) {
        WrappedRef<T>* wr = (WrappedRef<T>*) param;
        handle.Dispose();
    }

    static Local<Object> Create(Persistent<T> handle, bool weak, std::string tag = "") {
        static int count = 0;
        HandleScope scope;
        WrappedRef<T>* ref = new WrappedRef<T>(handle, weak, tag);

        Local<ObjectTemplate> wrapperTmpl = ObjectTemplate::New();
        wrapperTmpl->SetInternalFieldCount(1);
        Local<Object> wrapped = wrapperTmpl->NewInstance();
        wrapped->SetPointerInInternalField(0, ref);

        Persistent<Object> weakWrapped = Persistent<Object>::New(wrapped);
        weakWrapped.MakeWeak((void*) ref, WrappedRef<T>::FreeWrapped);
        return scope.Close(wrapped);
    }

    static void FreeWrapped(Persistent<Value> wrapped, void* param) {
        static int count = 0;
        WrappedRef<T>* wr = (WrappedRef<T>*)param;
        delete wr;
        wrapped.Dispose();
    }

    static Persistent<T> GetHandle(Local<Object> wrapped) {
        WrappedRef<T>* wr = 
            (WrappedRef<T>*) wrapped->GetPointerFromInternalField(0);
        return wr->m_handle;
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
    // Use a strong reference between sandbox -> wrapped, so that wrapped gets
    // GC'd when sandbox goes out of scope.
    Local<Object> wrapped = WrappedRef<Context>::Create(context, false, "strong context");
    sandbox->SetHiddenValue(String::New("wrapped"), wrapped);
    // Use a weak reference between global -> wrapped, or else wrapped will be
    // kept alive.
    Persistent<Context> weakContext = Persistent<Context>::New(context);
    Local<Object> wrappedWeak = WrappedRef<Context>::Create(weakContext, true, "weak context");
    context->Global()->SetHiddenValue(String::New("wrapped"), wrappedWeak);
    NODE_SET_METHOD(sandbox, "run", Run);
    NODE_SET_METHOD(sandbox, "getGlobal", GetGlobal);
    return scope.Close(sandbox);
}

// Create a context whose global object uses the sandbox to lookup and set
// properties.
static Persistent<Context> CreateContext(Local<Object> box) {
    HandleScope scope;
    Persistent<Object> sandbox = Persistent<Object>::New(box);
    Local<Object> wrapped = WrappedRef<Object>::Create(sandbox, true, "weak sandbox");

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

/*
 * args[0] = String of code
 * args[1] = filename
 */
static Handle<Value> Run(const Arguments& args) {
    HandleScope scope;
    Local<Object> wrapped =
        args.This()->GetHiddenValue(String::New("wrapped"))->ToObject();
    Persistent<Context> context = WrappedRef<Context>::GetHandle(wrapped);
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
    Local<Object> wrapped =
        args.This()->GetHiddenValue(String::New("wrapped"))->ToObject();
    Persistent<Context> context = WrappedRef<Context>::GetHandle(wrapped);
    return scope.Close(context->Global());
}

static Handle<Value> GlobalPropertyGetter (Local<String> property,
                                           const AccessorInfo &info) {
    HandleScope scope;
    Local<Object> wrapped = info.Data()->ToObject();
    Persistent<Object> sandbox = WrappedRef<Object>::GetHandle(wrapped);
    // First check the sandbox object.
    Local<Value> rv = sandbox->Get(property);
    if (rv->IsUndefined()) {
        // Next, check the global object (things like Object, Array, etc).
        Local<Object> wrapped =
            sandbox->GetHiddenValue(String::New("wrapped"))->ToObject();
        Persistent<Context> context = WrappedRef<Context>::GetHandle(wrapped);
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
    Local<Object> wrapped = info.Data()->ToObject();
    Persistent<Object> sandbox = WrappedRef<Object>::GetHandle(wrapped);
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
    Local<Object> wrapped = info.Data()->ToObject();
    Persistent<Object> sandbox = WrappedRef<Object>::GetHandle(wrapped);
    bool success = sandbox->Delete(property);
    if (!success) {
        Local<Object> wrapped =
            sandbox->GetHiddenValue(String::New("wrapped"))->ToObject();
        Persistent<Context> context = WrappedRef<Context>::GetHandle(wrapped);
        success = context->Global()->Delete(property);
    }
    return scope.Close(Boolean::New(success));
}

static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &info) {
    HandleScope scope;
    Local<Object> wrapped = info.Data()->ToObject();
    Persistent<Object> sandbox = WrappedRef<Object>::GetHandle(wrapped);
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
