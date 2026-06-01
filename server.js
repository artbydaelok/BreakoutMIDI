const http = require('http')
const fs = require('fs')
const path = require('path')
const { WebSocketServer } = require('ws')
let midi

try {
  midi = require('midi')
} catch(e) {
  console.error('node-midi not found. Run: npm install')
  process.exit(1)
}

// ── MIDI setup ──
const midiOut = new midi.Output()
const portCount = midiOut.getPortCount()
console.log(`\nAvailable MIDI outputs (${portCount}):`)
for (let i = 0; i < portCount; i++) console.log(` [${i}] ${midiOut.getPortName(i)}`)

if (portCount === 0) {
  console.error('\nNo MIDI outputs found. Is loopMIDI running?')
  process.exit(1)
}

const ports = []
for (let i = 0; i < portCount; i++) ports.push({ index: i, name: midiOut.getPortName(i) })

let currentPortIndex = null  // no port open until user picks one

function openPort(index) {
  if (currentPortIndex !== null) { try { midiOut.closePort() } catch(e){} }
  midiOut.openPort(index)
  currentPortIndex = index
  console.log(`MIDI out switched to: ${ports[index].name}`)
}

// ── HTTP server ──
const server = http.createServer((req, res) => {
  const file = path.join(__dirname, 'index.html')
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end('Not found'); return }
    res.writeHead(200, { 'Content-Type': 'text/html' })
    res.end(data)
  })
})

// ── WebSocket ──
const wss = new WebSocketServer({ server })
wss.on('connection', ws => {
  console.log('Browser connected')
  // send port list immediately
  ws.send(JSON.stringify({ type: 'ports', ports, current: currentPortIndex }))

  ws.on('message', raw => {
    try {
      const msg = JSON.parse(raw)
      if (msg.type === 'setPort') {
        openPort(msg.index)
        wss.clients.forEach(c => c.send(JSON.stringify({ type: 'ports', ports, current: currentPortIndex })))
        return
      }
      if (currentPortIndex === null) return  // no port selected yet
      const { note, vel, channel, duration } = msg
      const ch = ((channel || 1) - 1) & 0x0f
      midiOut.sendMessage([0x90 | ch, note, vel])
      setTimeout(() => { try { midiOut.sendMessage([0x80 | ch, note, 0]) } catch(e){} }, duration || 200)
    } catch(e) { console.error('Bad message:', e.message) }
  })
  ws.on('close', () => console.log('Browser disconnected'))
})

const PORT = 3000
server.listen(PORT, () => {
  console.log(`\nOpen in Chrome: http://localhost:${PORT}\n`)
})

process.on('exit', () => { try { midiOut.closePort() } catch(e){} })
