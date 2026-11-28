# R8 (release builds). The native code (libmain.so, SDL's libSDL3.so) calls Java by name through JNI, and the manifest
# names the activities: those keep their names and members. The rest of our Java is renamed and shrunk.

# SDL's Java side: the native code finds and calls its classes and methods by name
-keep class org.libsdl.app.** { *; }

# any class that declares a native method keeps those method names (JNI binds them by name)
-keepclasseswithmembernames,includedescriptorclasses class * {
    native <methods>;
}

# the activities, services and receivers the manifest starts (R8 keeps those already; listed for clarity)
-keep public class com.totkrecompandroid.TOTKRecompAndroidActivity
-keep public class com.totkrecompandroid.LoginActivity
-keep public class com.totkrecompandroid.DriverPickActivity
-keep public class com.totkrecompandroid.BuildService

# the attributes the libraries read at runtime
-keepattributes *Annotation*,Signature,InnerClasses,EnclosingMethod,SourceFile,LineNumberTable
