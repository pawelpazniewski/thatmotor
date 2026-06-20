package com.thatmotor.kayak

/** Load a JSON fixture from `src/test/resources/` by name. */
fun loadFixture(name: String): String {
    val stream = object {}.javaClass.classLoader?.getResourceAsStream(name)
        ?: error("fixture not found on test classpath: $name")
    return stream.bufferedReader().use { it.readText() }
}
