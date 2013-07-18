#include "node.h"
#include "node_version.h"
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
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        sandbox.Reset(isolate, sbox);
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
        context.Reset(Isolate::GetCurrent(), createV8Context());
        proxyGlobal.Reset(Isolate::GetCurrent(), Local<Context>::New(Isolate::GetCurrent(), context)->Global());
    }
    
    // This is an object that just keeps an internal pointer to this
    // ContextifyContext.  It's passed to the NamedPropertyHandler.  If we
    // pass the main JavaScript context object we're embedded in, then the
    // NamedPropertyHandler will store a reference to it forever and keep it
    // from getting gc'd.
    Local<Value> createDataWrapper () {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> wrapper = Local<Function>::New(isolate, dataWrapperCtor)->NewInstance();
#if NODE_MAJOR_VERSION > 0 || (NODE_MINOR_VERSION == 9 && (NODE_PATCH_VERSION >= 6 && NODE_PATCH_VERSION <= 10)) || NODE_MINOR_VERSION >= 11
        wrapper->SetAlignedPointerInInternalField(0, this);
#else
        wrapper->SetPointerInInternalField(0, this);
#endif
        return scope.Close(wrapper);
    }

    Handle<Context> createV8Context() {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<FunctionTemplate> ftmpl = FunctionTemplate::New();
        ftmpl->SetHiddenPrototype(true);
        ftmpl->SetClassName(Local<Object>::New(isolate, sandbox)->GetConstructorName());
        Local<ObjectTemplate> otmpl = ftmpl->InstanceTemplate();
        otmpl->SetNamedPropertyHandler(GlobalPropertyGetterCallback,
                                       GlobalPropertySetterCallback,
                                       GlobalPropertyQueryCallback,
                                       GlobalPropertyDeleterCallback,
                                       GlobalPropertyEnumeratorCallback,
                                       createDataWrapper());
        otmpl->SetAccessCheckCallbacks(GlobalPropertyNamedAccessCheck,
                                       GlobalPropertyIndexedAccessCheck);

        return scope.Close(Context::New(isolate, NULL, otmpl));
    }

    static void Init(Handle<Object> target) {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        dataWrapperTmpl.Reset(isolate, FunctionTemplate::New());
        Local<FunctionTemplate>::New(isolate, dataWrapperTmpl)->InstanceTemplate()->SetInternalFieldCount(1);
        dataWrapperCtor.Reset(isolate, Local<FunctionTemplate>::New(isolate, dataWrapperTmpl)->GetFunction());

        jsTmpl.Reset(isolate, FunctionTemplate::New(New));
        Local<FunctionTemplate> ljsTmpl =  Local<FunctionTemplate>::New(isolate, jsTmpl);
        ljsTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        ljsTmpl->SetClassName(String::NewSymbol("ContextifyContext"));

        NODE_SET_PROTOTYPE_METHOD(ljsTmpl, "run",       ContextifyContext::Run);
        NODE_SET_PROTOTYPE_METHOD(ljsTmpl, "getGlobal", ContextifyContext::GetGlobal);

        target->Set(String::NewSymbol("ContextifyContext"), ljsTmpl->GetFunction());
    }

    // info[0] = the sandbox object
    template<class T> static void New(const v8::FunctionCallbackInfo<T>& info) {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        if (info.Length() < 1) {
            Local<String> msg = String::New("Wrong number of arguments passed to ContextifyContext constructor");
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
        }
        if (!info[0]->IsObject()) {
            Local<String> msg = String::New("Argument to ContextifyContext constructor must be an object.");
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
        }
        ContextifyContext* ctx = new ContextifyContext(info[0]->ToObject());
        ctx->Wrap(info.This());
        info.GetReturnValue().Set(info.This());
    }

    template<class T> static void Run(const v8::FunctionCallbackInfo<T>& info) {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        if (info.Length() == 0) {
            Local<String> msg = String::New("Must supply at least 1 argument to run");
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
        }
        if (!info[0]->IsString()) {
            Local<String> msg = String::New("First argument to run must be a String.");
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
        }
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(info.This());
        Persistent<Context> context;
        context.Reset(isolate, ctx->context);
        Local<Context> lcontext = Local<Context>::New(isolate, context);
        lcontext->Enter();
        Local<String> code = info[0]->ToString();
        TryCatch trycatch;
        Handle<Script> script;
        if (info.Length() > 1 && info[1]->IsString()) {
            script = Script::Compile(code, info[1]->ToString());
        } else {
            script = Script::Compile(code);
        }
        if (script.IsEmpty()) {
          lcontext->Exit();
          info.GetReturnValue().Set(trycatch.ReThrow());
          return;
        }
        Handle<Value> result = script->Run();
        lcontext->Exit();
        if (result.IsEmpty()) {
            info.GetReturnValue().Set(trycatch.ReThrow());
            return;
        }
        info.GetReturnValue().Set(result);
    }

    static bool InstanceOf(Handle<Value> value) {
      return !value.IsEmpty() && Local<FunctionTemplate>::New(Isolate::GetCurrent(), jsTmpl)->HasInstance(value);
    }

    template<class T> static void GetGlobal(const v8::FunctionCallbackInfo<T>& info) {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(info.This());
        info.GetReturnValue().Set(ctx->proxyGlobal);
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

    static void GlobalPropertyGetterCallback (Local<String> property,
                                               const PropertyCallbackInfo<Value>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        Local<Value> rv = Local<Object>::New(isolate, ctx->sandbox)->GetRealNamedProperty(property);
        if (rv.IsEmpty()) {
            rv = Local<Object>::New(isolate, ctx->proxyGlobal)->GetRealNamedProperty(property);
        }
        info.GetReturnValue().Set(rv);
    }

    static void GlobalPropertySetterCallback (Local<String> property,
                                               Local<Value> value,
                                               const PropertyCallbackInfo<Value>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        Local<Object>::New(isolate, ctx->sandbox)->Set(property, value);
        info.GetReturnValue().Set(value);
    }

    static void GlobalPropertyQueryCallback(Local<String> property,
                                               const PropertyCallbackInfo<Integer>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        if (!Local<Object>::New(isolate, ctx->sandbox)->GetRealNamedProperty(property).IsEmpty() ||
            !Local<Object>::New(isolate, ctx->proxyGlobal)->GetRealNamedProperty(property).IsEmpty()) {
            info.GetReturnValue().Set(Integer::New(None));
        } else {
            info.GetReturnValue().Set(Handle<Integer>());
        }
    }

    static void GlobalPropertyDeleterCallback(Local<String> property,
                                                 const PropertyCallbackInfo<Boolean>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        bool success = Local<Object>::New(isolate, ctx->sandbox)->Delete(property);
        if (!success) {
            success = Local<Object>::New(isolate, ctx->proxyGlobal)->Delete(property);
        }
        info.GetReturnValue().Set(Boolean::New(success));
    }

    static void GlobalPropertyEnumeratorCallback(const PropertyCallbackInfo<Array>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
        info.GetReturnValue().Set(Local<Object>::New(isolate, ctx->sandbox)->GetPropertyNames());
    }
};

class ContextifyScript : ObjectWrap {
public:
    static Persistent<FunctionTemplate> scriptTmpl;
    Persistent<Script> script;

    static void Init(Handle<Object> target) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        scriptTmpl.Reset(isolate, FunctionTemplate::New(New));
        Local<FunctionTemplate> lscriptTmpl = Local<FunctionTemplate>::New(isolate, scriptTmpl);
        lscriptTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        lscriptTmpl->SetClassName(String::NewSymbol("ContextifyScript"));

        NODE_SET_PROTOTYPE_METHOD(lscriptTmpl, "runInContext", RunInContext);

        target->Set(String::NewSymbol("ContextifyScript"),
                    lscriptTmpl->GetFunction());
    }

    template<class T> static void New(const v8::FunctionCallbackInfo<T>& info) {
      Isolate* isolate = Isolate::GetCurrent();
      HandleScope scope(isolate);

      ContextifyScript *contextify_script = new ContextifyScript();
      contextify_script->Wrap(info.Holder());

      if (info.Length() < 1) {
        info.GetReturnValue().Set(ThrowException(Exception::TypeError(
              String::New("needs at least 'code' argument."))));
        return;
      }

      Local<String> code = info[0]->ToString();
      Local<String> filename = info.Length() > 1
                               ? info[1]->ToString()
                               : String::New("ContextifyScript.<anonymous>");

      Handle<Context> context = Context::GetCurrent();
      Context::Scope context_scope(context);

      // Catch errors
      TryCatch trycatch;

      Handle<Script> v8_script = Script::New(code, filename);

      if (v8_script.IsEmpty()) {
        info.GetReturnValue().Set(trycatch.ReThrow());
        return;
      }

      contextify_script->script.Reset(isolate, v8_script);

      info.GetReturnValue().Set(info.This());
    }

    template<class T> static void RunInContext(const v8::FunctionCallbackInfo<T>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        if (info.Length() == 0) {
            Local<String> msg = String::New("Must supply at least 1 argument to runInContext");
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
        }
        if (!ContextifyContext::InstanceOf(info[0]->ToObject())) {
            Local<String> msg = String::New("First argument must be a ContextifyContext.");
            info.GetReturnValue().Set(ThrowException(Exception::TypeError(msg)));
        }
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(info[0]->ToObject());
        Persistent<Context> context;
        context.Reset(isolate, ctx->context);
        Local<Context> lcontext = Local<Context>::New(isolate, context);
        lcontext->Enter();
        
        ContextifyScript* wrapped_script = ObjectWrap::Unwrap<ContextifyScript>(info.This());
        Handle<Script> script = Handle<Script>::New(isolate, wrapped_script->script);
        TryCatch trycatch;
        if (script.IsEmpty()) {
          lcontext->Exit();
          info.GetReturnValue().Set(trycatch.ReThrow());
          return;
        }
        Handle<Value> result = script->Run();
        lcontext->Exit();
        if (result.IsEmpty()) {
            info.GetReturnValue().Set(trycatch.ReThrow());
            return;
        }
        info.GetReturnValue().Set(result);
    }

    ~ContextifyScript() {
        script.Dispose();
    }
};

Persistent<FunctionTemplate> ContextifyContext::jsTmpl;
Persistent<FunctionTemplate> ContextifyScript::scriptTmpl;

extern "C" {
    static void init(Handle<Object> target) {
        ContextifyContext::Init(target);
        ContextifyScript::Init(target);
    }
    NODE_MODULE(contextify, init);
};
