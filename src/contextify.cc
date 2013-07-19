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
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        HandleScope scope;
        sandbox = Persistent<Object>::New(sbox);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        sandbox.Reset(isolate, sbox);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
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
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        context     = createV8Context();
        proxyGlobal = Persistent<Object>::New(context->Global());
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        context.Reset(Isolate::GetCurrent(), createV8Context());
        proxyGlobal.Reset(Isolate::GetCurrent(), Local<Context>::New(Isolate::GetCurrent(), context)->Global());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }
    
    // This is an object that just keeps an internal pointer to this
    // ContextifyContext.  It's passed to the NamedPropertyHandler.  If we
    // pass the main JavaScript context object we're embedded in, then the
    // NamedPropertyHandler will store a reference to it forever and keep it
    // from getting gc'd.
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    Local<Object> createDataWrapper () {
        HandleScope scope;
        Local<Object> wrapper = dataWrapperCtor->NewInstance();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    Local<Value> createDataWrapper () {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> wrapper = Local<Function>::New(isolate, dataWrapperCtor)->NewInstance();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
#if NODE_MAJOR_VERSION > 0 || (NODE_MINOR_VERSION == 9 && (NODE_PATCH_VERSION >= 6 && NODE_PATCH_VERSION <= 10)) || NODE_MINOR_VERSION >= 11
        wrapper->SetAlignedPointerInInternalField(0, this);
#else
        wrapper->SetPointerInInternalField(0, this);
#endif
        return scope.Close(wrapper);
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    Persistent<Context> createV8Context() {
        HandleScope scope;
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    Handle<Context> createV8Context() {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        Local<FunctionTemplate> ftmpl = FunctionTemplate::New();
        ftmpl->SetHiddenPrototype(true);
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        ftmpl->SetClassName(sandbox->GetConstructorName());
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ftmpl->SetClassName(Local<Object>::New(isolate, sandbox)->GetConstructorName());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        Local<ObjectTemplate> otmpl = ftmpl->InstanceTemplate();
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        otmpl->SetNamedPropertyHandler(GlobalPropertyGetter,
                                       GlobalPropertySetter,
                                       GlobalPropertyQuery,
                                       GlobalPropertyDeleter,
                                       GlobalPropertyEnumerator,
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        otmpl->SetNamedPropertyHandler(GlobalPropertyGetterCallback,
                                       GlobalPropertySetterCallback,
                                       GlobalPropertyQueryCallback,
                                       GlobalPropertyDeleterCallback,
                                       GlobalPropertyEnumeratorCallback,
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
                                       createDataWrapper());
        otmpl->SetAccessCheckCallbacks(GlobalPropertyNamedAccessCheck,
                                       GlobalPropertyIndexedAccessCheck);
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
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
#endif /* ! NODE_VERSION_AT_LEAST(0, 11, 4) */

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        NODE_SET_PROTOTYPE_METHOD(jsTmpl, "run",       ContextifyContext::Run);
        NODE_SET_PROTOTYPE_METHOD(jsTmpl, "getGlobal", ContextifyContext::GetGlobal);

        target->Set(String::NewSymbol("ContextifyContext"), jsTmpl->GetFunction());
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        return scope.Close(Context::New(isolate, NULL, otmpl));
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    // args[0] = the sandbox object
    static Handle<Value> New(const Arguments& args) {
        HandleScope scope;
        if (args.Length() < 1) {
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
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
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            Local<String> msg = String::New("Wrong number of arguments passed to ContextifyContext constructor");
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return ThrowException(Exception::Error(msg));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        if (!args[0]->IsObject()) {
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (!info[0]->IsObject()) {
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            Local<String> msg = String::New("Argument to ContextifyContext constructor must be an object.");
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return ThrowException(Exception::Error(msg));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        ContextifyContext* ctx = new ContextifyContext(args[0]->ToObject());
        ctx->Wrap(args.This());
        return args.This();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = new ContextifyContext(info[0]->ToObject());
        ctx->Wrap(info.This());
        info.GetReturnValue().Set(info.This());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Value> Run(const Arguments& args) {
        HandleScope scope;
        if (args.Length() == 0) {
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    template<class T> static void Run(const v8::FunctionCallbackInfo<T>& info) {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        if (info.Length() == 0) {
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            Local<String> msg = String::New("Must supply at least 1 argument to run");
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return ThrowException(Exception::Error(msg));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        if (!args[0]->IsString()) {
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (!info[0]->IsString()) {
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            Local<String> msg = String::New("First argument to run must be a String.");
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return ThrowException(Exception::Error(msg));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args.This());
        Persistent<Context> context = ctx->context;
        context->Enter();
        Local<String> code = args[0]->ToString();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(info.This());
        Persistent<Context> context;
        context.Reset(isolate, ctx->context);
        Local<Context> lcontext = Local<Context>::New(isolate, context);
        lcontext->Enter();
        Local<String> code = info[0]->ToString();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        TryCatch trycatch;
        Handle<Script> script;
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        if (args.Length() > 1 && args[1]->IsString()) {
            script = Script::Compile(code, args[1]->ToString());
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (info.Length() > 1 && info[1]->IsString()) {
            script = Script::Compile(code, info[1]->ToString());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        } else {
            script = Script::Compile(code);
        }
        if (script.IsEmpty()) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
          context->Exit();
          return trycatch.ReThrow();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
          lcontext->Exit();
          info.GetReturnValue().Set(trycatch.ReThrow());
          return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
        Handle<Value> result = script->Run();
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        context->Exit();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        lcontext->Exit();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (result.IsEmpty()) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return trycatch.ReThrow();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(trycatch.ReThrow());
            return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        return scope.Close(result);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        info.GetReturnValue().Set(result);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

    static bool InstanceOf(Handle<Value> value) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
      return !value.IsEmpty() && jsTmpl->HasInstance(value);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      return !value.IsEmpty() && Local<FunctionTemplate>::New(Isolate::GetCurrent(), jsTmpl)->HasInstance(value);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Value> GetGlobal(const Arguments& args) {
        HandleScope scope;
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args.This());
        return ctx->proxyGlobal;
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    template<class T> static void GetGlobal(const v8::FunctionCallbackInfo<T>& info) {
        Isolate *isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(info.This());
        info.GetReturnValue().Set(ctx->proxyGlobal);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
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

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Value> GlobalPropertyGetter (Local<String> property,
                                               const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    static void GlobalPropertyGetterCallback (Local<String> property,
                                               const PropertyCallbackInfo<Value>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        Local<Value> rv = ctx->sandbox->GetRealNamedProperty(property);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        Local<Value> rv = Local<Object>::New(isolate, ctx->sandbox)->GetRealNamedProperty(property);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (rv.IsEmpty()) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            rv = ctx->proxyGlobal->GetRealNamedProperty(property);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            rv = Local<Object>::New(isolate, ctx->proxyGlobal)->GetRealNamedProperty(property);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        return scope.Close(rv);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        info.GetReturnValue().Set(rv);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Value> GlobalPropertySetter (Local<String> property,
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    static void GlobalPropertySetterCallback (Local<String> property,
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
                                               Local<Value> value,
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
                                               const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
                                               const PropertyCallbackInfo<Value>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        ctx->sandbox->Set(property, value);
        return scope.Close(value);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        Local<Object>::New(isolate, ctx->sandbox)->Set(property, value);
        info.GetReturnValue().Set(value);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Integer> GlobalPropertyQuery(Local<String> property,
                                               const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    static void GlobalPropertyQueryCallback(Local<String> property,
                                               const PropertyCallbackInfo<Integer>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        if (!ctx->sandbox->GetRealNamedProperty(property).IsEmpty() ||
            !ctx->proxyGlobal->GetRealNamedProperty(property).IsEmpty()) {
            return scope.Close(Integer::New(None));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (!Local<Object>::New(isolate, ctx->sandbox)->GetRealNamedProperty(property).IsEmpty() ||
            !Local<Object>::New(isolate, ctx->proxyGlobal)->GetRealNamedProperty(property).IsEmpty()) {
            info.GetReturnValue().Set(Integer::New(None));
        } else {
            info.GetReturnValue().Set(Handle<Integer>());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        return scope.Close(Handle<Integer>());
#endif /* ! NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Boolean> GlobalPropertyDeleter(Local<String> property,
                                                 const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    static void GlobalPropertyDeleterCallback(Local<String> property,
                                                 const PropertyCallbackInfo<Boolean>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        bool success = ctx->sandbox->Delete(property);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        bool success = Local<Object>::New(isolate, ctx->sandbox)->Delete(property);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (!success) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            success = ctx->proxyGlobal->Delete(property);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            success = Local<Object>::New(isolate, ctx->proxyGlobal)->Delete(property);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        return scope.Close(Boolean::New(success));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        info.GetReturnValue().Set(Boolean::New(success));
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Array> GlobalPropertyEnumerator(const AccessorInfo &accessInfo) {
        HandleScope scope;
        Local<Object> data = accessInfo.Data()->ToObject();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    static void GlobalPropertyEnumeratorCallback(const PropertyCallbackInfo<Array>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        Local<Object> data = info.Data()->ToObject();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(data);
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        return scope.Close(ctx->sandbox->GetPropertyNames());
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        info.GetReturnValue().Set(Local<Object>::New(isolate, ctx->sandbox)->GetPropertyNames());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }
};

class ContextifyScript : ObjectWrap {
public:
    static Persistent<FunctionTemplate> scriptTmpl;
    Persistent<Script> script;

    static void Init(Handle<Object> target) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        HandleScope scope;
        scriptTmpl = Persistent<FunctionTemplate>::New(FunctionTemplate::New(New));
        scriptTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        scriptTmpl->SetClassName(String::NewSymbol("ContextifyScript"));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        scriptTmpl.Reset(isolate, FunctionTemplate::New(New));
        Local<FunctionTemplate> lscriptTmpl = Local<FunctionTemplate>::New(isolate, scriptTmpl);
        lscriptTmpl->InstanceTemplate()->SetInternalFieldCount(1);
        lscriptTmpl->SetClassName(String::NewSymbol("ContextifyScript"));
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        NODE_SET_PROTOTYPE_METHOD(scriptTmpl, "runInContext", RunInContext);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        NODE_SET_PROTOTYPE_METHOD(lscriptTmpl, "runInContext", RunInContext);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */

        target->Set(String::NewSymbol("ContextifyScript"),
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
                    scriptTmpl->GetFunction());
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
                    lscriptTmpl->GetFunction());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Value> New(const Arguments& args) {
      HandleScope scope;
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    template<class T> static void New(const v8::FunctionCallbackInfo<T>& info) {
      Isolate* isolate = Isolate::GetCurrent();
      HandleScope scope(isolate);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */

      ContextifyScript *contextify_script = new ContextifyScript();
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
      contextify_script->Wrap(args.Holder());
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      contextify_script->Wrap(info.Holder());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
      if (args.Length() < 1) {
        return ThrowException(Exception::TypeError(
              String::New("needs at least 'code' argument.")));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      if (info.Length() < 1) {
        info.GetReturnValue().Set(ThrowException(Exception::TypeError(
              String::New("needs at least 'code' argument."))));
        return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
      Local<String> code = args[0]->ToString();
      Local<String> filename = args.Length() > 1
                               ? args[1]->ToString()
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      Local<String> code = info[0]->ToString();
      Local<String> filename = info.Length() > 1
                               ? info[1]->ToString()
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
                               : String::New("ContextifyScript.<anonymous>");

      Handle<Context> context = Context::GetCurrent();
      Context::Scope context_scope(context);

      // Catch errors
      TryCatch trycatch;

      Handle<Script> v8_script = Script::New(code, filename);

      if (v8_script.IsEmpty()) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        return trycatch.ReThrow();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        info.GetReturnValue().Set(trycatch.ReThrow());
        return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
      contextify_script->script = Persistent<Script>::New(v8_script);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      contextify_script->script.Reset(isolate, v8_script);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
      return args.This();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
      info.GetReturnValue().Set(info.This());
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    }

#if !NODE_VERSION_AT_LEAST(0, 11, 4)
    static Handle<Value> RunInContext(const Arguments& args) {
        HandleScope scope;
        if (args.Length() == 0) {
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
    template<class T> static void RunInContext(const v8::FunctionCallbackInfo<T>& info) {
        Isolate* isolate = Isolate::GetCurrent();
        HandleScope scope(isolate);
        if (info.Length() == 0) {
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            Local<String> msg = String::New("Must supply at least 1 argument to runInContext");
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return ThrowException(Exception::Error(msg));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(ThrowException(Exception::Error(msg)));
            return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        if (!ContextifyContext::InstanceOf(args[0]->ToObject())) {
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (!ContextifyContext::InstanceOf(info[0]->ToObject())) {
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            Local<String> msg = String::New("First argument must be a ContextifyContext.");
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return ThrowException(Exception::TypeError(msg));
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(ThrowException(Exception::TypeError(msg)));
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(args[0]->ToObject());
        Persistent<Context> context = ctx->context;
        context->Enter();
        ContextifyScript* wrapped_script = ObjectWrap::Unwrap<ContextifyScript>(args.This());
        Handle<Script> script = wrapped_script->script;
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        ContextifyContext* ctx = ObjectWrap::Unwrap<ContextifyContext>(info[0]->ToObject());
        Persistent<Context> context;
        context.Reset(isolate, ctx->context);
        Local<Context> lcontext = Local<Context>::New(isolate, context);
        lcontext->Enter();
        
        ContextifyScript* wrapped_script = ObjectWrap::Unwrap<ContextifyScript>(info.This());
        Handle<Script> script = Handle<Script>::New(isolate, wrapped_script->script);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        TryCatch trycatch;
        if (script.IsEmpty()) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
          context->Exit();
          return trycatch.ReThrow();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
          lcontext->Exit();
          info.GetReturnValue().Set(trycatch.ReThrow());
          return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
        Handle<Value> result = script->Run();
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        context->Exit();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        lcontext->Exit();
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        if (result.IsEmpty()) {
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
            return trycatch.ReThrow();
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
            info.GetReturnValue().Set(trycatch.ReThrow());
            return;
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        }
#if !NODE_VERSION_AT_LEAST(0, 11, 4)
        return scope.Close(result);
#else /* NODE_VERSION_AT_LEAST(0, 11, 4) */
        info.GetReturnValue().Set(result);
#endif /* NODE_VERSION_AT_LEAST(0, 11, 4) */
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
