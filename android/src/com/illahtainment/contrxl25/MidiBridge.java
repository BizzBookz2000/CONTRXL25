package com.illahtainment.contrxl25;

import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.webkit.JavascriptInterface;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/**
 * MIDI keyboards for the web app. Android's WebView has no Web MIDI, so android-shim.js gives the page a
 * navigator.requestMIDIAccess() whose inputs are fed from here:
 *   CXMidi.list()          -> JSON [{id,name,manufacturer}] of the MIDI sources on the device
 *   window.__cxMidiIn(id, [bytes])   one complete MIDI message (called on the UI thread)
 *   window.__cxMidiChanged(json)     a keyboard was plugged in / removed
 * Works with USB MIDI keyboards (USB-C or an OTG adapter) and with Bluetooth MIDI devices that Android
 * has already connected (for example through the maker's app).
 */
public class MidiBridge {
    private final MainActivity act;
    private final MidiManager mm;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Map<Integer, Opened> open = new HashMap<>();
    private boolean started;

    private static class Opened {
        MidiDevice device;
        final ArrayList<MidiOutputPort> ports = new ArrayList<>();
        String name;
    }

    MidiBridge(MainActivity a) {
        act = a;
        mm = (MidiManager) a.getSystemService(Context.MIDI_SERVICE);   // null when the device has no MIDI support
    }

    private final MidiManager.DeviceCallback callback = new MidiManager.DeviceCallback() {
        @Override public void onDeviceAdded(MidiDeviceInfo info) { openDevice(info); notifyPage(); }
        @Override public void onDeviceRemoved(MidiDeviceInfo info) { closeDevice(info.getId()); notifyPage(); }
    };

    void start() {
        if (mm == null || started) return;
        started = true;
        mm.registerDeviceCallback(callback, ui);
        for (MidiDeviceInfo info : mm.getDevices()) openDevice(info);
    }

    void stop() {
        if (mm == null) return;
        try { mm.unregisterDeviceCallback(callback); } catch (Exception ignored) {}
        for (Integer id : new ArrayList<>(open.keySet())) closeDevice(id);
        started = false;
    }

    static String nameOf(MidiDeviceInfo info) {
        Bundle p = info.getProperties();
        String n = p.getString(MidiDeviceInfo.PROPERTY_NAME);
        if (n == null || n.isEmpty()) n = p.getString(MidiDeviceInfo.PROPERTY_PRODUCT);
        if (n == null || n.isEmpty()) n = "MIDI device " + info.getId();
        return n;
    }

    private void openDevice(final MidiDeviceInfo info) {
        if (info.getOutputPortCount() == 0 || open.containsKey(info.getId())) return;   // nothing to read from
        final Opened o = new Opened();
        o.name = nameOf(info);
        open.put(info.getId(), o);
        mm.openDevice(info, new MidiManager.OnDeviceOpenedListener() {
            @Override public void onDeviceOpened(MidiDevice device) {
                if (device == null) { open.remove(info.getId()); return; }
                o.device = device;
                for (int p = 0; p < info.getOutputPortCount(); ++p) {
                    MidiOutputPort port = device.openOutputPort(p);
                    if (port == null) continue;
                    String id = info.getId() + ":" + p;
                    String nm = info.getOutputPortCount() > 1 ? o.name + " " + (p + 1) : o.name;
                    port.connect(new Framer(id, nm));
                    o.ports.add(port);
                }
                notifyPage();
            }
        }, ui);
    }

    private void closeDevice(int id) {
        Opened o = open.remove(id);
        if (o == null) return;
        for (MidiOutputPort p : o.ports) try { p.close(); } catch (IOException ignored) {}
        if (o.device != null) try { o.device.close(); } catch (IOException ignored) {}
    }

    @JavascriptInterface public String list() {
        StringBuilder b = new StringBuilder("[");
        if (mm != null) {
            boolean first = true;
            for (MidiDeviceInfo info : mm.getDevices()) {
                int n = info.getOutputPortCount();
                for (int p = 0; p < n; ++p) {
                    if (!first) b.append(',');
                    first = false;
                    String nm = n > 1 ? nameOf(info) + " " + (p + 1) : nameOf(info);
                    String mf = info.getProperties().getString(MidiDeviceInfo.PROPERTY_MANUFACTURER);
                    b.append("{\"id\":\"").append(info.getId()).append(':').append(p)
                     .append("\",\"name\":\"").append(esc(nm))
                     .append("\",\"manufacturer\":\"").append(esc(mf == null ? "" : mf)).append("\"}");
                }
            }
        }
        return b.append(']').toString();
    }

    @JavascriptInterface public boolean available() { return mm != null; }

    static String esc(String s) { return s.replace("\\", "\\\\").replace("\"", "\\\"").replaceAll("[\\x00-\\x1f]", " "); }

    private void notifyPage() {
        final String json = list();
        ui.post(new Runnable() { @Override public void run() {
            act.web.evaluateJavascript("window.__cxMidiChanged&&window.__cxMidiChanged(" + json + ")", null);
        }});
    }

    /** Splits the byte stream of one port into whole MIDI messages (running status, SysEx, real-time). */
    private class Framer extends MidiReceiver {
        private final String id, jsId;
        private final byte[] msg = new byte[3];
        private int status, need, have;
        private StringBuilder sysex;

        Framer(String id, String name) { this.id = id; this.jsId = "'" + id + "'"; }

        @Override public void onSend(byte[] data, int offset, int count, long ts) {
            for (int k = offset; k < offset + count; ++k) {
                int b = data[k] & 0xFF;
                if (b >= 0xF8) { send(new int[] { b }); continue; }            // clock, start, stop... (inside anything)
                if (b == 0xF0) { sysex = new StringBuilder("240"); continue; }
                if (sysex != null) {
                    if (b == 0xF7) { sysex.append(",247"); sendRaw(sysex.toString()); sysex = null; }
                    else if (b < 0x80) sysex.append(',').append(b);
                    else sysex = null;                                         // broken SysEx: drop it
                    if (sysex != null || b == 0xF7) continue;
                }
                if (b >= 0x80) {
                    status = b; have = 0;
                    int hi = b & 0xF0;
                    need = (hi == 0xC0 || hi == 0xD0 || b == 0xF1 || b == 0xF3) ? 1 : (b == 0xF6 || b == 0xF7) ? 0 : 2;
                    if (need == 0) { send(new int[] { b }); status = 0; }
                    continue;
                }
                if (status == 0) continue;                                     // data byte with no status
                msg[have++] = (byte) b;
                if (have == need) {
                    send(need == 1 ? new int[] { status, msg[0] & 0xFF } : new int[] { status, msg[0] & 0xFF, msg[1] & 0xFF });
                    have = 0;
                    if (status >= 0xF0) status = 0;                            // system common: no running status
                }
            }
        }

        private void send(int[] m) {
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < m.length; ++i) { if (i > 0) s.append(','); s.append(m[i]); }
            sendRaw(s.toString());
        }

        private void sendRaw(final String bytes) {
            final String js = "window.__cxMidiIn&&window.__cxMidiIn(" + jsId + ",[" + bytes + "])";
            ui.post(new Runnable() { @Override public void run() { act.web.evaluateJavascript(js, null); } });
        }
    }
}
