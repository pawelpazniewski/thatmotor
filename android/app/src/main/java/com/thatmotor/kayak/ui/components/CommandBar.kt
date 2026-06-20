package com.thatmotor.kayak.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.thatmotor.kayak.data.Command
import com.thatmotor.kayak.domain.CommandAvailability
import com.thatmotor.kayak.ui.theme.DangerRed
import com.thatmotor.kayak.ui.theme.DeepWater
import com.thatmotor.kayak.ui.theme.WarnAmber

/**
 * The operational command bar: ARM / DISARM / DEPLOY / STOW. Each button is enabled
 * only when [availability] permits it (e.g. ARM disabled when the link is down) and no
 * command is already in flight ([isSending]) — disabling the whole bar while a POST is
 * pending prevents a double-tap from firing parallel commands (coding-rules pkt 13).
 * DEPLOY and STOW move physical hardware, so they go through a confirmation dialog
 * before [onCommand] fires (mirrors the web panel's guarded deploy/stow).
 */
@Composable
fun CommandBar(
    availability: CommandAvailability,
    isSending: Boolean,
    onCommand: (Command) -> Unit,
    modifier: Modifier = Modifier,
) {
    var pendingConfirm by remember { mutableStateOf<Command?>(null) }

    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        CommandButton(
            label = "ARM",
            enabled = availability.canArm && !isSending,
            color = WarnAmber,
            onClick = { onCommand(Command.ARM) },
            modifier = Modifier.weight(1f),
        )
        CommandButton(
            label = "DISARM",
            enabled = availability.canDisarm && !isSending,
            color = DangerRed,
            onClick = { onCommand(Command.DISARM) },
            modifier = Modifier.weight(1f),
        )
        CommandButton(
            label = "DEPLOY",
            enabled = availability.canDeploy && !isSending,
            color = DeepWater,
            onClick = { pendingConfirm = Command.DEPLOY },
            modifier = Modifier.weight(1f),
        )
        CommandButton(
            label = "STOW",
            enabled = availability.canStow && !isSending,
            color = DeepWater,
            onClick = { pendingConfirm = Command.STOW },
            modifier = Modifier.weight(1f),
        )
    }

    pendingConfirm?.let { command ->
        ConfirmDialog(
            command = command,
            onConfirm = {
                pendingConfirm = null
                onCommand(command)
            },
            onDismiss = { pendingConfirm = null },
        )
    }
}

@Composable
private fun CommandButton(
    label: String,
    enabled: Boolean,
    color: androidx.compose.ui.graphics.Color,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Button(
        onClick = onClick,
        enabled = enabled,
        colors = ButtonDefaults.buttonColors(containerColor = color),
        modifier = modifier.height(64.dp),
    ) {
        Text(text = label, fontWeight = FontWeight.Bold, fontSize = 18.sp)
    }
}

@Composable
private fun ConfirmDialog(command: Command, onConfirm: () -> Unit, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = "Confirm ${command.keyword.uppercase()}") },
        text = { Text(text = confirmMessage(command)) },
        confirmButton = {
            TextButton(onClick = onConfirm) { Text(text = command.keyword.uppercase()) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(text = "Cancel") }
        },
    )
}

private fun confirmMessage(command: Command): String = when (command) {
    Command.DEPLOY -> "Raise the motor and switch the drive on?"
    Command.STOW -> "Stow the motor and stop the drive?"
    Command.ARM, Command.DISARM -> "Confirm ${command.keyword}?"
}
