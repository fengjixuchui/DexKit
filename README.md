<div align="center">
    <h1> DexKit </h1>

[![license](https://img.shields.io/github/license/LuckyPray/DexKit.svg)](https://www.gnu.org/licenses/lgpl-3.0.html)
[![](https://jitpack.io/v/LuckyPray/DexKit.svg)](https://jitpack.io/#LuckyPray/DexKit)

[README](https://github.com/LuckyPray/DexKit/blob/master/README.md)|[中文说明](https://github.com/LuckyPray/DexKit/blob/master/README_zh.md)

</div>

An easy-to-use, high-performance dex deobfuscation library. Easy to use your CMAKE/Android projects.

## API introduction

There are two APIs can meet most of your usage scenarios:

- **`DexKit::BatchFindClassesUsingStrings`**
- **`DexKit::BatchFindMethodsUsingStrings`**

> **Note**: In all cases you should avoid searching for keywords that contain duplicate content, eg: {"key_word", "word"}, as this will cause tags to be overwritten, resulting in inaccurate search results.
> If there is such a need, open the advanced search mode as much as possible, and use the string to match the content exactly, for example, modify it to this: {"^key_word$", "^word$"}

And there are many other APIs:

- `DexKit::FindMethodCaller`: find caller for specified method.
- `DexKit::FindMethodInvoking`: find the called method
- `DexKit::FindMethodUsingField`: Find method to get/set specified field
- `DexKit::FindMethodUsingString`: find method used utf8 string
- `DexKit::FindMethod`: find method by multiple conditions
- `DexKit::FindSubClasses`: find all direct subclasses of the specified class
- `DexKit::FindMethodUsingOpPrefixSeq`:  find all method using opcode prefix sequence(op range: `0x00`-`0xFF`)
- `DexKit::FindMethodUsingOpCodeSeq`: find all method using opcode sequence(op range: `0x00`-`0xFF`)
- `DexKit::GetMethodOpCodeSeq`: get method opcode sequence(op range: `0x00`-`0xFF`)

> **Note**: At present, all instructions are only for standard dex instructions and do not include odex optimization instructions.

For more detailed instructions, please refer to [dex_kit.h](https://github.com/LuckyPray/DexKit/blob/master/Core/include/dex_kit.h).

## Quick start

### Method 1: Direct introduction (recommended)

However, this approach will import an extra `so` file. If you don't want to import an extra `so` file, please use the second/third method.

`build.gradle`:
```groovy
allprojects {
    repositories {
        ...
        maven { url 'https://jitpack.io' }
    }
}
```

`app/build.gradle`:
```groovy
dependencies {
    implementation 'com.github.LuckyPray:DexKit:<version>'
}
```

#### JAVA Example

`DexKitBridge` provides 2 factory methods to create `Dexkit` instances:

- `DexKitBridge.create(apkPath)`: normally, please use it.
- `DexKitBridge.create(classLoader, true)`: for reinforced apps, used `classLoader` and set option useCookieDexFile to true.

PS: `DexKitBridge.create(classLoader, false)` ≈ `DexKitBridge.create(apkPath)`, but the former may contain part of the system dex.

> **Note**: for normally apps, using `DexKitBridge.create(classLoader, true)` may be a problem.
> Because the dexfile in cookies may be modified (dex2oat), currently DexKit cannot be parsed odex's `quick` instruction.

```java 
import io.luckypry.dexkit.DexKitBridge;
// ...

public class DexUtil {

    static {
        System.loadLibrary("dexkit");
    }

    public static void findMethod() {
        // for no-reinforced apps please use apkpath to load, because of the exist of dex2oat and CompactDex(cdex),
        // dexkit currently only handles StandardDex.
        String apkPath = application.applicationInfo.sourceDir
        // try-with-resources, auto close DexKitBridge, no need to call DexKitBridge.close()
        // if you don't use try-with-resources, be sure to manually call DexKitBridge.close() to release the jni memory
        try (DexKitBridge dexKitBridge = DexKitBridge.create(apkPath)) {
            if (dexKitBridge == null) {
                Log.e("DexUtil", "DexKitBridge create failed");
                return;
            }
            List<DexClassDescriptor> classes = dexKitBridge.findSubClasses("android.app.Activity", null);
            for (DexClassDescriptor clazz : classes) {
                String name = clazz.getName();
                String simpleName = clazz.getSimpleName();
                Class<?> clz = clazz.getClassInstance(hostClassLoader);
                Log.i("DexUtil", "findSubClasses: " + clz);
            }
        } catch (Throwable e) {
            Log.e("DexUtil", Log.getStackTraceString(e));
        }
    }
}
```

### Method 2：google prefab
`app/build.gradle`:

```groovy
android {
    buildFeatures {
        prefab true
    }
}
```

> **Note**: DexKit-Android uses the [prefab package schema v2](https://github.com/google/prefab/releases/tag/v2.0.0),
which is configured by default since [Android Gradle Plugin 7.1.0](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#7-1-0).
If you are using Android Gradle Plugin earlier than 7.1.0, please add the following configuration to `gradle.properties`:

```
android.prefabVersion=2.0.0
```

Also avoid `libdexkit.so` being added to the apk, you can add the following configuration to `app/build.gradle`:
```groovy
android {
    packagingOptions {
        jniLibs.excludes.add("lib/**/libdexkit.so")
    }
}
```

CMake:

You can use `find_package` in `CMakeLists.txt` to use DexKit:
```cmake
add_library(my_lib SHARED native.cpp)

# Add two lines below, must contain libz!!
find_package(dexkit REQUIRED CONFIG)
target_link_libraries(my_lib dexkit::dex_kit_static z)
```


At the same time, we also provide [dex_kit_jni_helper.h](https://github.com/LuckyPray/DexKit/blob/master/Core/include/dex_kit_jni_helper.h) 
to transition java to c++ / c++ to java data objects.
```c++
#include <jni.h>
#include <dex_kit.h>
#include "dex_kit_jni_helper.h"

#define DEXKIT_JNI extern "C" JNIEXPORT JNICALL

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
```

### Method 3: git submodule

reference: https://github.com/LuckyPray/XAutoDaily/tree/master/dexkit

## Example

- [main.cpp](https://github.com/LuckyPray/DexKit/blob/master/Core/main.cpp)
- [qq-example.cpp](https://github.com/LuckyPray/DexKit/blob/master/Core/qq-example.cpp)

## Benchmark

qq-example.cpp in MacPro M1 to deobfuscate `qq-8.9.3.apk`, the result is:

```txt
findClass count: 47
findMethod count: 29
used time: 207 ms
```

## License

The slicer directory is partially copied from [AOSP](https://cs.android.com/android/platform/superproject/+/master:frameworks/base/startop/view_compiler).

Modified parts are owed by LuckyPray Developers. If you would like to use it in an open source project, please submodule it.
