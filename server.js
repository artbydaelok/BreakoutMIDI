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

// ── MIDI Output ──
const midiOut = new midi.Output()
const outCount = midiOut.getPortCount()
console.log(`\nMIDI outputs (${outCount}):`)
for (let i = 0; i < outCount; i++) console.log(` [${i}] ${midiOut.getPortName(i)}`)

if (outCount === 0) { console.error('\nNo MIDI outputs found.'); process.exit(1) }

const outPorts = []
for (let i = 0; i < outCount; i++) outPorts.push({ index: i, name: midiOut.getPortName(i) })

let currentOutIndex = null
function openOutPort(index) {
  if (currentOutIndex !== null) { try { midiOut.closePort() } catch(e){} }
  midiOut.openPort(index)
  currentOutIndex = index
  console.log(`MIDI out: ${outPorts[index].name}`)
}

// ── MIDI Input ──
const midiIn = new midi.Input()
const inCount = midiIn.getPortCount()
console.log(`\nMIDI inputs (${inCount}):`)
for (let i = 0; i < inCount; i++) console.log(` [${i}] ${midiIn.getPortName(i)}`)

const inPorts = []
for (let i = 0; i < inCount; i++) inPorts.push({ index: i, name: midiIn.getPortName(i) })

let currentInIndex = null
midiIn.ignoreTypes(false, false, false)

function openInPort(index) {
  if (currentInIndex !== null) { try { midiIn.closePort() } catch(e){} }
  midiIn.openPort(index)
  currentInIndex = index
  console.log(`MIDI in: ${inPorts[index].name}`)
}

// Forward incoming MIDI notes to all browser clients
midiIn.on('message', (deltaTime, message) => {
  const [status, note, vel] = message
  const type = status & 0xf0
  const isNoteOn  = type === 0x90 && vel > 0
  const isNoteOff = type === 0x80 || (type === 0x90 && vel === 0)
  if (isNoteOn || isNoteOff) {
    const payload = JSON.stringify({ type: 'midiIn', note, vel, on: isNoteOn })
    wss.clients.forEach(c => { if (c.readyState === 1) c.send(payload) })
  }
})

// ── HTTP ──
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

function broadcastPorts() {
  const payload = JSON.stringify({ type: 'ports', outPorts, inPorts, currentOut: currentOutIndex, currentIn: currentInIndex })
  wss.clients.forEach(c => { if (c.readyState === 1) c.send(payload) })
}

wss.on('connection', ws => {
  console.log('Browser connected')
  ws.send(JSON.stringify({ type: 'ports', outPorts, inPorts, currentOut: currentOutIndex, currentIn: currentInIndex }))

  ws.on('message', raw => {
    try {
      const msg = JSON.parse(raw)
      if (msg.type === 'setOutPort') { openOutPort(msg.index); broadcastPorts(); return }
      if (msg.type === 'setInPort')  { openInPort(msg.index);  broadcastPorts(); return }
      if (currentOutIndex === null) return
      const { note, vel, channel, duration } = msg
      const ch = ((channel || 1) - 1) & 0x0f
      midiOut.sendMessage([0x90 | ch, note, vel])
      setTimeout(() => { try { midiOut.sendMessage([0x80 | ch, note, 0]) } catch(e){} }, duration || 200)
    } catch(e) { console.error('Bad message:', e.message) }
  })
  ws.on('close', () => console.log('Browser disconnected'))
})

const PORT = 3000
server.listen(PORT, () => console.log(`\nOpen: http://localhost:${PORT}\n`))

process.on('exit', () => {
  try { midiOut.closePort() } catch(e){}
  try { midiIn.closePort() } catch(e){}
})
