package com.thatmotor.kayak.data

import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Test

class CommandTest {

    @Test
    fun `CommandRequest from Command maps ARM to the arm keyword`() {
        // Arrange / Act
        val request = CommandRequest(Command.ARM)

        // Assert
        assertEquals("arm", request.cmd)
    }

    @Test
    fun `CommandRequest from ARM serializes to the wire body`() {
        // Arrange
        val request = CommandRequest(Command.ARM)

        // Act
        val json = Json.encodeToString(CommandRequest.serializer(), request)

        // Assert: exact wire shape the firmware POST /api/command expects.
        assertEquals("""{"cmd":"arm"}""", json)
    }

    @Test
    fun `Command keywords match the firmware command_parse table`() {
        // Oracle: these are the exact strings the firmware matches in
        // components/web_panel/src/command_parse.c (COMMAND_TABLE). Renaming an
        // enum keyword would silently break the contract — this test fails first.
        val expected = mapOf(
            Command.ARM to "arm",
            Command.DISARM to "disarm",
            Command.DEPLOY to "deploy",
            Command.STOW to "stow",
        )

        // Act / Assert: every entry maps to its firmware keyword...
        expected.forEach { (command, keyword) ->
            assertEquals(keyword, command.keyword)
        }
        // ...and the set is exactly these four (no added/removed commands).
        assertEquals(expected.keys, Command.entries.toSet())
    }
}
