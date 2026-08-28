# Keep the public SDK surface stable for Maven consumers.
-keep public class com.hxcplayer.HXCPlayerControl { *; }
-keep public class com.hxcplayer.HXCPlayerControl$* { *; }
-keep public class com.hxcplayer.HXCLinkedPlayerCoordinator { *; }
-keep public class com.hxcplayer.HXCLinkedPlayerCoordinator$* { *; }
-keep public class com.hxcplayer.AudioOutputState { *; }
-keep public class com.hxcplayer.HXCPlayerLicenseManager { *; }
-keep public class com.hxcplayer.HXCPlayerLicenseManager$* { *; }
-keep public class com.hxcplayer.download.** { *; }
-keep class com.hxcplayer.monitor.** { *; }

# JNI bindings in libhxcplayer.so depend on these class and method names.
-keepclasseswithmembernames class com.hxcplayer.HXCPlayerControl {
    native <methods>;
}
-keep class com.hxcplayer.HXCPlayerControl {
    native <methods>;
}

# Preserve enum lookup methods used by callers and Kotlin generated code.
-keepclassmembers enum com.hxcplayer.** {
    public static **[] values();
    public static ** valueOf(java.lang.String);
}

# Keep Kotlin metadata useful for Kotlin consumers.
-keep class kotlin.Metadata { *; }
