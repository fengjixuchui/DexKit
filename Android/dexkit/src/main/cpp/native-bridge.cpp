#include <jni.h>
#include <android/log.h>
#include "dex_kit.h"
#include "dex_kit_jni_helper.h"

#define TAG "DexKit"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL, TAG ,__VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG ,__VA_ARGS__)

#define DEXKIT_JNI extern "C" JNIEXPORT JNICALL

static jfieldID path_list_field = nullptr;
static jfieldID element_field = nullptr;
static jfieldID dex_file_field = nullptr;
static jfieldID cookie_field = nullptr;
static jfieldID file_name_field = nullptr;
static bool is_initialized = false;

struct DexFile {
    const void *begin_{};
    size_t size_{};

    virtual ~DexFile() = default;
};

static bool IsDexFile(const void *image) {
    const auto *header = reinterpret_cast<const struct dex::Header *>(image);
    if (header->magic[0] == 'd' && header->magic[1] == 'e' &&
        header->magic[2] == 'x' && header->magic[3] == '\n') {
        return true;
    }
    return false;
}

void init(JNIEnv *env) {
    if (is_initialized) {
        return;
    }
    auto dex_class_loader = env->FindClass("dalvik/system/BaseDexClassLoader");
    path_list_field = env->GetFieldID(dex_class_loader, "pathList",
                                      "Ldalvik/system/DexPathList;");
    auto dex_path_list = env->FindClass("dalvik/system/DexPathList");
    element_field = env->GetFieldID(dex_path_list, "dexElements",
                                    "[Ldalvik/system/DexPathList$Element;");
    auto element = env->FindClass("dalvik/system/DexPathList$Element");
    dex_file_field =
            env->GetFieldID(element, "dexFile", "Ldalvik/system/DexFile;");
    auto dex_file = env->FindClass("dalvik/system/DexFile");
    cookie_field = env->GetFieldID(dex_file, "mCookie", "Ljava/lang/Object;");
    file_name_field = env->GetFieldID(dex_file, "mFileName", "Ljava/lang/String;");

    is_initialized = true;
}

DEXKIT_JNI jlong
Java_io_luckypray_dexkit_DexKitBridge_nativeInitDexKit(JNIEnv *env, jclass clazz,
                                                       jstring apk_path) {
    if (!apk_path) {
        return 0;
    }
    const char *cStr = env->GetStringUTFChars(apk_path, nullptr);
    LOGI("apkPath -> %s", cStr);
    std::string filePathStr(cStr);
    auto dexkit = new dexkit::DexKit(filePathStr);
    env->ReleaseStringUTFChars(apk_path, cStr);
    return (jlong) dexkit;
}

DEXKIT_JNI jlong
Java_io_luckypray_dexkit_DexKitBridge_nativeInitDexKitByClassLoader(JNIEnv *env, jclass clazz,
                                                                    jobject class_loader) {
    if (!class_loader) {
        return 0;
    }
    init(env);
    auto path_list = env->GetObjectField(class_loader, path_list_field);
    if (!path_list)
        return 0;
    auto elements = (jobjectArray) env->GetObjectField(path_list, element_field);
    if (!elements)
        return 0;
    LOGD("elements size -> %d", env->GetArrayLength(elements));
    std::vector<std::pair<const void *, size_t>> images;
    std::list<dexkit::MemMap> maps;
    std::string apk_path;
    for (auto i = 0, len = env->GetArrayLength(elements); i < len; ++i) {
        auto element = env->GetObjectArrayElement(elements, i);
        if (!element)
            continue;
        auto java_dex_file = env->GetObjectField(element, dex_file_field);
        if (!java_dex_file)
            continue;
        auto cookie = (jlongArray) env->GetObjectField(java_dex_file, cookie_field);
        if (!cookie)
            continue;
        auto dex_file_length = env->GetArrayLength(cookie);
        const auto *dex_files = reinterpret_cast<const DexFile **>(
                env->GetLongArrayElements(cookie, nullptr));
        LOGI("dex_file_length -> %d", dex_file_length);
        std::vector<std::pair<const void *, size_t>> dex_images;
        if (!dex_files[0]) {
            while (dex_file_length-- > 1) {
                const auto *dex_file = dex_files[dex_file_length];
                LOGD("Got dex file %d", dex_file_length);
                if (!dex_file) {
                    LOGD("Skip empty dex file");
                    dex_images.clear();
                    continue;
                }
                if (!IsDexFile(dex_file->begin_)) {
                    LOGD("skip compact dex");
                    dex_images.clear();
                    break;
                } else {
                    LOGD("push dex file %d, image size: %zu", dex_file_length, dex_file->size_);
                    dex_images.emplace_back(dex_file->begin_, dex_file->size_);
                }
            }
        }
        if (dex_images.empty() && apk_path.empty()) {
            auto file_name_obj = (jstring) env->GetObjectField(java_dex_file, file_name_field);
            if (!file_name_obj) continue;
            auto file_name = env->GetStringUTFChars(file_name_obj, nullptr);
            LOGD("dex filename is %s", file_name);
            std::string path(file_name);
            if (path.find(".apk") == path.size() - 4) {
                apk_path = path;
            }
            env->ReleaseStringUTFChars(file_name_obj, file_name);
            env->DeleteLocalRef(file_name_obj);
        } else {
            for (auto &image: dex_images) {
                images.emplace_back(std::move(image));
            }
        }
    }
    if (images.empty()) {
        if (apk_path.empty()) {
            LOGW("dex file and apk_path not found");
            return 0;
        }
        LOGD("contains compact dex or not found cookie, use apk_path load: %s", apk_path.c_str());
        return (jlong) new dexkit::DexKit(apk_path);
    }
    return (jlong) new dexkit::DexKit(images);
}

extern "C"
JNIEXPORT void JNICALL
Java_io_luckypray_dexkit_DexKitBridge_nativeSetThreadNum(JNIEnv *env, jclass clazz,
                                                         jlong native_ptr, jint thread_num) {
    SetThreadNum(env, native_ptr, thread_num);
}

DEXKIT_JNI jint
Java_io_luckypray_dexkit_DexKitBridge_nativeGetDexNum(JNIEnv *env, jclass clazz,
                                                      jlong native_ptr) {
    return GetDexNum(env, native_ptr);
}

DEXKIT_JNI void
Java_io_luckypray_dexkit_DexKitBridge_nativeRelease(JNIEnv *env, jclass clazz,
                                                    jlong native_ptr) {
    ReleaseDexKitInstance(env, native_ptr);
}

DEXKIT_JNI jobject
Java_io_luckypray_dexkit_DexKitBridge_nativeBatchFindClassesUsingStrings(JNIEnv *env,
                                                                         jclass clazz,
                                                                         jlong native_ptr,
                                                                         jobject map,
                                                                         jboolean advanced_match,
                                                                         jintArray dex_priority) {
    return BatchFindClassesUsingStrings(env, native_ptr, map, advanced_match, dex_priority);
}

DEXKIT_JNI jobject
Java_io_luckypray_dexkit_DexKitBridge_nativeBatchFindMethodsUsingStrings(JNIEnv *env,
                                                                         jclass clazz,
                                                                         jlong native_ptr,
                                                                         jobject map,
                                                                         jboolean advanced_match,
                                                                         jintArray dex_priority) {
    return BatchFindMethodsUsingStrings(env, native_ptr, map, advanced_match, dex_priority);
}

DEXKIT_JNI jobjectArray
Java_io_luckypray_dexkit_DexKitBridge_nativeFindMethodCaller(JNIEnv *env, jclass clazz,
                                                             jlong native_ptr,
                                                             jstring method_descriptor,
                                                             jstring method_declare_class,
                                                             jstring method_declare_name,
                                                             jstring method_return_type,
                                                             jobjectArray method_param_types,
                                                             jstring caller_method_declare_class,
                                                             jstring caller_method_declare_name,
                                                             jstring caller_method_return_type,
                                                             jobjectArray caller_method_param_types,
                                                             jintArray dex_priority) {
    return FindMethodCaller(env, native_ptr, method_descriptor, method_declare_class,
                            method_declare_name, method_return_type, method_param_types,
                            caller_method_declare_class, caller_method_declare_name,
                            caller_method_return_type, caller_method_param_types, dex_priority);
}

DEXKIT_JNI jobject
Java_io_luckypray_dexkit_DexKitBridge_nativeFindMethodInvoking(JNIEnv *env, jclass clazz,
                                                               jlong native_ptr,
                                                               jstring method_descriptor,
                                                               jstring method_declare_class,
                                                               jstring method_declare_name,
                                                               jstring method_return_type,
                                                               jobjectArray method_param_types,
                                                               jstring be_called_method_declare_class,
                                                               jstring be_called_method_declare_name,
                                                               jstring be_called_method_return_type,
                                                               jobjectArray be_called_method_param_types,
                                                               jintArray dex_priority) {
    return FindMethodInvoking(env, native_ptr, method_descriptor, method_declare_class,
                              method_declare_name, method_return_type, method_param_types,
                              be_called_method_declare_class, be_called_method_declare_name,
                              be_called_method_return_type, be_called_method_param_types,
                              dex_priority);
}

DEXKIT_JNI jobject
Java_io_luckypray_dexkit_DexKitBridge_nativeFindMethodUsingField(JNIEnv *env, jclass clazz,
                                                                 jlong native_ptr,
                                                                 jstring field_descriptor,
                                                                 jstring field_declare_class,
                                                                 jstring field_name,
                                                                 jstring field_type,
                                                                 jint used_flags,
                                                                 jstring caller_method_declare_class,
                                                                 jstring caller_method_name,
                                                                 jstring caller_method_return_type,
                                                                 jobjectArray caller_method_param_types,
                                                                 jintArray dex_priority) {
    return FindMethodUsingField(env, native_ptr, field_descriptor, field_declare_class, field_name,
                                field_type, used_flags, caller_method_declare_class,
                                caller_method_name, caller_method_return_type,
                                caller_method_param_types, dex_priority);
}

DEXKIT_JNI jobjectArray
Java_io_luckypray_dexkit_DexKitBridge_nativeFindMethodUsingString(JNIEnv *env, jclass clazz,
                                                                  jlong native_ptr,
                                                                  jstring used_string,
                                                                  jboolean advanced_match,
                                                                  jstring method_declare_class,
                                                                  jstring method_name,
                                                                  jstring method_return_type,
                                                                  jobjectArray method_param_types,
                                                                  jintArray dex_priority) {
    return FindMethodUsingString(env, native_ptr, used_string, advanced_match, method_declare_class,
                                 method_name, method_return_type, method_param_types, dex_priority);
}

DEXKIT_JNI jobjectArray
Java_io_luckypray_dexkit_DexKitBridge_nativeFindMethod(JNIEnv *env, jclass clazz,
                                                       jlong native_ptr,
                                                       jstring method_declare_class,
                                                       jstring method_name,
                                                       jstring method_return_type,
                                                       jobjectArray method_param_types,
                                                       jintArray dex_priority) {
    return FindMethod(env, native_ptr, method_declare_class, method_name, method_return_type,
                      method_param_types, dex_priority);
}

DEXKIT_JNI jobjectArray
Java_io_luckypray_dexkit_DexKitBridge_nativeFindSubClasses(JNIEnv *env, jclass clazz,
                                                           jlong native_ptr,
                                                           jstring parent_class,
                                                           jintArray dex_priority) {
    return FindSubClasses(env, native_ptr, parent_class, dex_priority);
}

DEXKIT_JNI jobjectArray
Java_io_luckypray_dexkit_DexKitBridge_nativeFindMethodOpPrefixSeq(JNIEnv *env, jclass clazz,
                                                                  jlong native_ptr,
                                                                  jintArray op_prefix_seq,
                                                                  jstring method_declare_class,
                                                                  jstring method_name,
                                                                  jstring method_return_type,
                                                                  jobjectArray method_param_types,
                                                                  jintArray dex_priority) {
    return FindMethodOpPrefixSeq(env, native_ptr, op_prefix_seq, method_declare_class, method_name,
                                 method_return_type, method_param_types, dex_priority);
}

extern "C"
JNIEXPORT jobjectArray JNICALL
Java_io_luckypray_dexkit_DexKitBridge_nativeFindMethodUsingOpCodeSeq(JNIEnv *env, jclass clazz,
                                                                     jlong native_ptr,
                                                                     jintArray op_seq,
                                                                     jstring method_declare_class,
                                                                     jstring method_name,
                                                                     jstring method_return_type,
                                                                     jobjectArray method_param_types,
                                                                     jintArray dex_priority) {
    return FindMethodUsingOpCodeSeq(env, native_ptr, op_seq, method_declare_class, method_name,
                                    method_return_type, method_param_types, dex_priority);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_io_luckypray_dexkit_DexKitBridge_nativeGetMethodOpCodeSeq(JNIEnv *env, jclass clazz,
                                                               jlong native_ptr,
                                                               jstring method_descriptor,
                                                               jstring method_declare_class,
                                                               jstring method_name,
                                                               jstring method_return_type,
                                                               jobjectArray method_param_types,
                                                               jintArray dex_priority) {
    return GetMethodOpCodeSeq(env, native_ptr, method_descriptor, method_declare_class, method_name,
                              method_return_type, method_param_types, dex_priority);
}