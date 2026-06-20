# kotlinx.serialization: keep generated serializers for @Serializable classes.
-keepattributes RuntimeVisibleAnnotations,AnnotationDefault
-keepclassmembers class com.thatmotor.kayak.** {
    *** Companion;
}
-keepclasseswithmembers class com.thatmotor.kayak.** {
    kotlinx.serialization.KSerializer serializer(...);
}
