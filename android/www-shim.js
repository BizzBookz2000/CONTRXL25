// CONTRXL 25 for Android - loaded before the app. Fills in what Android's WebView lacks.
(function () {
  'use strict';
  // ---- Web MIDI from the Android MIDI bridge (USB / connected Bluetooth MIDI keyboards)
  // always replace: Android's WebView may expose requestMIDIAccess without a working MIDI service
  if (window.CXMidi) {
    var inputs = new Map(), outputs = new Map(), access = null;
    function makePort(d) {
      var listeners = [];
      return {
        id: d.id, name: d.name, manufacturer: d.manufacturer || '', type: 'input', version: '',
        state: 'connected', connection: 'open', onmidimessage: null, onstatechange: null,
        open: function () { return Promise.resolve(this); },
        close: function () { return Promise.resolve(this); },
        addEventListener: function (t, f) { if (t === 'midimessage') listeners.push(f); },
        removeEventListener: function (t, f) { var i = listeners.indexOf(f); if (i >= 0) listeners.splice(i, 1); },
        _emit: function (bytes) {
          var ev = { data: Uint8Array.from(bytes), timeStamp: performance.now(), target: this, currentTarget: this };
          if (typeof this.onmidimessage === 'function') { try { this.onmidimessage(ev); } catch (e) { console.error(e); } }
          for (var i = 0; i < listeners.length; i++) { try { listeners[i](ev); } catch (e) { console.error(e); } }
        }
      };
    }
    function sync(list) {
      var seen = {}, changed = [];
      for (var i = 0; i < list.length; i++) {
        var d = list[i]; seen[d.id] = 1;
        if (!inputs.has(d.id)) { var p = makePort(d); inputs.set(d.id, p); changed.push(p); }
      }
      inputs.forEach(function (p, id) {
        if (!seen[id]) { p.state = 'disconnected'; p.connection = 'closed'; inputs.delete(id); changed.push(p); }
      });
      return changed;
    }
    function read() { try { return JSON.parse(window.CXMidi.list()); } catch (e) { return []; } }
    window.__cxMidiIn = function (id, bytes) { var p = inputs.get(id); if (p) p._emit(bytes); };
    window.__cxMidiChanged = function (list) {
      var ch = sync(list || read());
      if (access) for (var i = 0; i < ch.length; i++) {
        var ev = { port: ch[i], target: access };
        if (typeof access.onstatechange === 'function') { try { access.onstatechange(ev); } catch (e) { console.error(e); } }
      }
    };
    var request = function () {
      if (!access) {
        sync(read());
        access = { inputs: inputs, outputs: outputs, sysexEnabled: true, onstatechange: null,
                   addEventListener: function () {}, removeEventListener: function () {} };
      }
      return Promise.resolve(access);
    };
    try { Object.defineProperty(navigator, 'requestMIDIAccess', { value: request, configurable: true, writable: true }); }
    catch (e) { navigator.requestMIDIAccess = request; }
  }

  // ---- Saving: the app saves through <a download href="blob:...">; send the file to Downloads/CONTRXL 25
  if (window.CXAndroid) {
    window.showSaveFilePicker = undefined;
    var CHUNK = 512 * 1024;
    function readChunk(blob) {
      return new Promise(function (ok, bad) {
        var r = new FileReader();
        r.onload = function () { var s = String(r.result); ok(s.slice(s.indexOf(',') + 1)); };
        r.onerror = function () { bad(r.error); };
        r.readAsDataURL(blob);
      });
    }
    async function saveBlob(blob, name) {
      window.CXAndroid.saveBegin(name || 'CONTRXL25', blob.type || '');
      for (var pos = 0; pos < blob.size; pos += CHUNK)
        window.CXAndroid.saveChunk(await readChunk(blob.slice(pos, pos + CHUNK)));
      return window.CXAndroid.saveEnd();
    }
    window.__cxSaveBlob = saveBlob;
    function handle(a) {
      var href = a.href || '';
      if (!a.hasAttribute('download') || !/^(blob|data):/i.test(href)) return false;
      var name = a.getAttribute('download') || 'CONTRXL25';
      fetch(href).then(function (r) { return r.blob(); }).then(function (b) { return saveBlob(b, name); })
        .catch(function (e) { console.error('save failed', e); });
      return true;
    }
    var nativeClick = HTMLAnchorElement.prototype.click;
    HTMLAnchorElement.prototype.click = function () { if (!handle(this)) nativeClick.call(this); };
    document.addEventListener('click', function (e) {
      var a = e.target && e.target.closest ? e.target.closest('a[download]') : null;
      if (a && handle(a)) e.preventDefault();
    }, true);
  }
  document.documentElement.classList.add('cx-android');
})();
