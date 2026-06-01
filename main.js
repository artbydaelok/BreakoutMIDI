const { app, BrowserWindow, ipcMain } = require('electron')
const path = require('path')

let midiOutput = null
let virtualPortOpen = false

function initMIDI() {
  try {
    const midi = require('midi')
    midiOutput = new midi.Output()

    if (process.platform !== 'win32') {
      // Mac/Linux: create a virtual port — appears in DAW automatically
      midiOutput.openVirtualPort('Breakout MIDI')
      virtualPortOpen = true
      console.log('[MIDI] Virtual port "Breakout MIDI" created')
    } else {
      // Windows: open first available output, or warn
      const count = midiOutput.getPortCount()
      if (count > 0) {
        midiOutput.openPort(0)
        console.log('[MIDI] Opened port:', midiOutput.getPor