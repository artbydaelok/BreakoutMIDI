const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('midi', {
  sendNote: (note, velocity, channel, duration) =>
    ipcRenderer.send('midi:note', { note, velocity, channel, duration }),
  getStatus: () =>
    ipcRenderer.invoke('midi:status'),
})
