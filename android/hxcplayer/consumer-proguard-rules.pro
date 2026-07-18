# Required when apps consuming the SDK enable minification.
-keep public class com.hxcplayer.HXCPlayerControl { *; }
-keep public class com.hxcplayer.HXCPlayerControl$* { *; }
-keep public class com.hxcplayer.HXCLinkedPlayerCoordinator { *; }
-keep public class com.hxcplayer.HXCLinkedPlayerCoordinator$* { *; }
-keep public class com.hxcplayer.AudioOutputState { *; }
-keep public class com.hxcplayer.HXCPlayerLicenseManager { *; }
-keep public class com.hxcplayer.HXCPlayerLicenseManager$* { *; }
-keep public class com.hxcplayer.download.** { *; }

-keepclasseswithmembernames class com.hxcplayer.HXCPlayerControl {
    native <methods>;
}
-keep class com.hxcplayer.HXCPlayerControl {
    native <methods>;
}

-keepclassmembers enum com.hxcplayer.** {
    public static **[] values();
    public static ** valueOf(java.lang.String);
}
