package com.fyrbyadditive.smartflasher.ui.screens

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Info
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.fyrbyadditive.smartflasher.R
import com.fyrbyadditive.smartflasher.ui.components.BaudRateSelector
import com.fyrbyadditive.smartflasher.ui.components.FileSelector
import com.fyrbyadditive.smartflasher.ui.components.PortSelector
import com.fyrbyadditive.smartflasher.ui.components.ProgressIndicator
import com.fyrbyadditive.smartflasher.ui.components.SerialMonitorPanel
import com.fyrbyadditive.smartflasher.ui.components.StatusDisplay
import com.fyrbyadditive.smartflasher.ui.components.StatusType
import com.fyrbyadditive.smartflasher.ui.theme.FAMESmartFlasherTheme

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    onNavigateToAbout: () -> Unit,
    viewModel: HomeViewModel = viewModel()
) {
    val context = LocalContext.current

    // Collect state
    val availablePorts by viewModel.usbDeviceManager.availablePorts.collectAsState()
    val selectedPort by viewModel.selectedPort.collectAsState()
    val selectedBaudRate by viewModel.selectedBaudRate.collectAsState()
    val firmwareFile by viewModel.firmwareFile.collectAsState()
    val flashingState by viewModel.flashingState.collectAsState()
    val progress by viewModel.progress.collectAsState()
    val isSerialMonitorEnabled by viewModel.isSerialMonitorEnabled.collectAsState()
    val serialMonitorOutput by viewModel.serialMonitorOutput.collectAsState()
    val isSerialMonitorConnected by viewModel.isSerialMonitorConnected.collectAsState()

    var showAdvanced by remember { mutableStateOf(false) }

    // File picker launcher
    val filePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        uri?.let {
            try {
                val inputStream = context.contentResolver.openInputStream(uri)
                val data = inputStream?.readBytes() ?: ByteArray(0)
                inputStream?.close()

                val fileName = uri.lastPathSegment ?: "firmware.bin"
                viewModel.setFirmware(uri, fileName, data)
            } catch (e: Exception) {
                // Handle error
            }
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.app_name)) },
                actions = {
                    IconButton(onClick = onNavigateToAbout) {
                        Icon(
                            imageVector = Icons.Default.Info,
                            contentDescription = stringResource(R.string.about)
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer,
                    titleContentColor = MaterialTheme.colorScheme.onPrimaryContainer
                )
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // Port Selection
            PortSelector(
                ports = availablePorts,
                selectedPort = selectedPort,
                onPortSelected = { viewModel.selectPort(it) },
                onRefresh = { viewModel.usbDeviceManager.refreshPorts() },
                enabled = !flashingState.isActive
            )

            // Firmware Selection
            FileSelector(
                fileName = firmwareFile?.fileName,
                fileSize = firmwareFile?.sizeDescription,
                onSelectFile = { filePickerLauncher.launch("application/octet-stream") },
                enabled = !flashingState.isActive
            )

            // Advanced Settings (collapsible)
            TextButton(
                onClick = { showAdvanced = !showAdvanced },
                enabled = !flashingState.isActive
            ) {
                Text(
                    text = if (showAdvanced) "Hide Advanced Settings" else stringResource(R.string.advanced_settings)
                )
            }

            AnimatedVisibility(
                visible = showAdvanced,
                enter = expandVertically() + fadeIn(),
                exit = shrinkVertically() + fadeOut()
            ) {
                BaudRateSelector(
                    selectedBaudRate = selectedBaudRate.value,
                    onBaudRateSelected = { viewModel.selectBaudRate(it) },
                    enabled = !flashingState.isActive
                )
            }

            Spacer(modifier = Modifier.height(8.dp))

            // Progress
            ProgressIndicator(
                progress = progress,
                isActive = flashingState.isActive || flashingState is FlashingState.Complete
            )

            // Status Message
            StatusDisplay(
                message = flashingState.statusMessage,
                statusType = when (flashingState) {
                    is FlashingState.Idle -> StatusType.IDLE
                    is FlashingState.Complete -> StatusType.SUCCESS
                    is FlashingState.Error -> StatusType.ERROR
                    else -> StatusType.ACTIVE
                }
            )

            Spacer(modifier = Modifier.height(8.dp))

            // Flash Button
            Button(
                onClick = {
                    if (flashingState.isActive) {
                        viewModel.cancelFlashing()
                    } else {
                        viewModel.startFlashing()
                    }
                },
                enabled = viewModel.canFlash || flashingState.isActive,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(56.dp),
                colors = if (flashingState.isActive) {
                    ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
                } else {
                    ButtonDefaults.buttonColors()
                }
            ) {
                if (flashingState.isActive) {
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        CircularProgressIndicator(
                            modifier = Modifier.size(20.dp),
                            strokeWidth = 2.dp,
                            color = MaterialTheme.colorScheme.onError
                        )
                        Text(stringResource(R.string.cancel))
                    }
                } else {
                    Text(stringResource(R.string.flash_firmware))
                }
            }

            // Serial Monitor Toggle
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Checkbox(
                    checked = isSerialMonitorEnabled,
                    onCheckedChange = { viewModel.toggleSerialMonitor() },
                    enabled = !flashingState.isActive
                )
                Text(
                    text = stringResource(R.string.show_serial_monitor),
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(start = 8.dp)
                )
            }

            // Serial Monitor Panel
            AnimatedVisibility(
                visible = isSerialMonitorEnabled,
                enter = expandVertically() + fadeIn(),
                exit = shrinkVertically() + fadeOut()
            ) {
                SerialMonitorPanel(
                    output = serialMonitorOutput,
                    isConnected = isSerialMonitorConnected,
                    onClear = { viewModel.clearSerialOutput() }
                )
            }
        }
    }
}

@Preview(showBackground = true)
@Composable
private fun HomeScreenPreview() {
    FAMESmartFlasherTheme {
        // Preview would need a mock ViewModel
    }
}
