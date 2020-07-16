#include <iostream>
#include "gtest/gtest.h"
#include "v8.h"
#include "libplatform/libplatform.h"

using namespace v8;

void print_data(StartupData* startup_data) {
  size_t size = static_cast<size_t>(startup_data->raw_size);
  for (size_t i = 0; i < size; i++) {
    char endchar = i != size - 1 ? ',' : '\n';
    std::cout << std::to_string(startup_data->data[i]) << endchar;
  }
}

class SnapshotTest : public ::testing::Test {
 public:
 protected:
  static std::unique_ptr<v8::Platform> platform_;
  static void SetUpTestCase() {
    platform_ = v8::platform::NewDefaultPlatform();
    v8::V8::SetFlagsFromString("--random_seed=42");
    v8::V8::InitializePlatform(platform_.get());
    v8::V8::Initialize();
  }
  static void TearDownTestCase() {
    v8::V8::ShutdownPlatform();
  }
};

std::unique_ptr<v8::Platform> SnapshotTest::platform_;

TEST_F(SnapshotTest, CreateSnapshot) {
  StartupData startup_data;
  size_t index;

  std::vector<intptr_t> external_references = { reinterpret_cast<intptr_t>(nullptr)};
  {
    Isolate* isolate = Isolate::Allocate();
    v8::SnapshotCreator snapshot_creator(isolate, external_references.data());
    {
      HandleScope scope(isolate);
      snapshot_creator.SetDefaultContext(Context::New(isolate));
      Local<Context> context = Context::New(isolate);
      {
        Context::Scope context_scope(context);
        TryCatch try_catch(isolate);

        // Add the following function to the context
        const char* js = R"(function test_snapshot() { 
          return 'from test_snapshot function';
        })";
        Local<v8::String> src = String::NewFromUtf8(isolate, js).ToLocalChecked();
        ScriptOrigin origin(String::NewFromUtf8Literal(isolate, "function"));
        ScriptCompiler::Source source(src, origin);                     
        Local<v8::Script> script;
        EXPECT_TRUE(ScriptCompiler::Compile(context, &source).ToLocal(&script));
        EXPECT_FALSE(script->Run(context).ToLocalChecked().IsEmpty());
        EXPECT_FALSE(try_catch.HasCaught());
      }

      index = snapshot_creator.AddContext(context);
      std::cout << "context index: " << index << '\n';
    }
    startup_data = snapshot_creator.CreateBlob(SnapshotCreator::FunctionCodeHandling::kKeep);
    std::cout << "size of blob: " << startup_data.raw_size << '\n';
  }
  // SnapshotCreators destructor will call Isolate::Exit and Isolate::Dispose

  Isolate::CreateParams create_params;
  // Specify the startup_data blob created earlier.
  create_params.snapshot_blob = &startup_data;
  create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  Isolate* isolate = Isolate::New(create_params);
  {
    HandleScope scope(isolate);
    // Create the Context from the snapshot index.
    Local<Context> context = Context::FromSnapshot(isolate, index).ToLocalChecked();
    Context::Scope context_scope(context);
    TryCatch try_catch(isolate);
    // Add JavaScript code that calls the function we added previously.
    const char* js = "test_snapshot();";
    Local<v8::String> src = String::NewFromUtf8(isolate, js).ToLocalChecked();
    ScriptOrigin origin(String::NewFromUtf8Literal(isolate, "usage"));
    ScriptCompiler::Source source(src, origin);                     
    Local<v8::Script> script;
    EXPECT_TRUE(ScriptCompiler::Compile(context, &source).ToLocal(&script));
    MaybeLocal<Value> maybe_result = script->Run(context);
    EXPECT_FALSE(maybe_result.IsEmpty());

    Local<Value> result = maybe_result.ToLocalChecked();
    String::Utf8Value utf8(isolate, result);
    EXPECT_STREQ("from test_snapshot function", *utf8);
    EXPECT_FALSE(try_catch.HasCaught());
  }
  isolate->Dispose();
}


void InternalFieldsCallback(Local<Object> holder, int index,
                            StartupData payload, void* data) {
  std::cout << "InternalFieldsCallback..." << '\n';
}

class ExternalData {
 public:
  ExternalData(int x) : x_(x) {}
  int x() { return x_; }
 private:
  int x_;
};

TEST_F(SnapshotTest, CreateSnapshotWithData) {
  StartupData startup_data;
  size_t index;
  size_t data_index;

  std::vector<intptr_t> external_references = { reinterpret_cast<intptr_t>(nullptr)};
  {
    Isolate* isolate = nullptr;
    isolate = Isolate::Allocate();
    v8::SnapshotCreator snapshot_creator(isolate, external_references.data());
    {
      HandleScope scope(isolate);
      snapshot_creator.SetDefaultContext(Context::New(isolate));
      Local<Context> context = Context::New(isolate);
      Local<Number> data = Number::New(isolate, 18);
      data_index = snapshot_creator.AddData(context, data);
      std::cout << "data_index: " << data_index << '\n';
      index = snapshot_creator.AddContext(context);
      std::cout << "context index: " << index << '\n';
    }
    startup_data = snapshot_creator.CreateBlob(SnapshotCreator::FunctionCodeHandling::kKeep);
    std::cout << "size of blob: " << startup_data.raw_size << '\n';
  }

  Isolate::CreateParams create_params;
  // Specify the startup_data blob created earlier.
  create_params.snapshot_blob = &startup_data;
  create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
  Isolate* isolate = Isolate::New(create_params);
  {
    HandleScope scope(isolate);
    const void* data = "some data";
    DeserializeInternalFieldsCallback int_fields_cb(InternalFieldsCallback,
                                                    const_cast<void*>(data));
    // Create the Context from the snapshot index.
    Local<Context> context = Context::FromSnapshot(isolate,
      index,
      int_fields_cb).ToLocalChecked();
    Context::Scope context_scope(context);
    MaybeLocal<Object> obj = context->GetDataFromSnapshotOnce<Object>(data_index);
    EXPECT_FALSE(obj.IsEmpty());
    Local<Object> o = obj.ToLocalChecked();
    Local<Number> nr = o.As<Number>();
    std::cout << "data: " << nr->Value() << '\n';
    EXPECT_EQ(18, nr->Value());
  }
  isolate->Dispose();
}

