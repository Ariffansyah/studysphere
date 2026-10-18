// StudySphere desktop: an island at the top centre of the screen, with an owl that keeps an eye
// on the study room, shows the node's telemetry and sends it lamp and LCD commands.
// The app is its own MQTT broker on port 1883 of this laptop, so nothing else needs installing:
// the node connects to 127.0.0.1 in WireLab's local simulator, or to this laptop's WiFi IP from a
// real ESP32. If another broker (Mosquitto) already has the port, the app uses that one instead.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use rumqttc::{Client, Event, MqttOptions, Packet, QoS};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::net::{Ipv4Addr, TcpListener};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;
use tauri::{AppHandle, Emitter, LogicalSize, Manager, PhysicalPosition, State, WebviewWindow};

const PORT: u16 = 1883;
static CONNECTED: AtomicBool = AtomicBool::new(false);
/// The island is a layer-shell surface: the compositor keeps it at the top centre.
#[cfg(target_os = "linux")]
static LAYER: AtomicBool = AtomicBool::new(false);

struct Mqtt {
    client: Client,
    own_broker: bool,
}

/// Starts the built-in broker on every interface; false when the port is taken by another broker.
fn start_broker() -> bool {
    // rumqttd only logs a failed bind, so check the port first.
    if TcpListener::bind((Ipv4Addr::UNSPECIFIED, PORT)).is_err() {
        return false;
    }
    let server = rumqttd::ServerSettings {
        name: "v4".into(),
        listen: (Ipv4Addr::UNSPECIFIED, PORT).into(),
        tls: None,
        next_connection_delay_ms: 1,
        // ponytail: anonymous, like the Mosquitto setup in the README; anyone on the same WiFi can
        // read and command the node. Set `auth` here and MQTT_USER/MQTT_PASS in config.h if that matters.
        connections: rumqttd::ConnectionSettings {
            connection_timeout_ms: 60000,
            max_payload_size: 20480,
            max_inflight_count: 100,
            auth: None,
            external_auth: None,
            dynamic_filters: true,
        },
    };
    let config = rumqttd::Config {
        router: rumqttd::RouterConfig {
            max_connections: 100,
            max_outgoing_packet_count: 200,
            max_segment_size: 1 << 20,
            max_segment_count: 10,
            ..Default::default()
        },
        v4: Some(HashMap::from([("1".into(), server)])),
        ..Default::default()
    };
    let mut broker = rumqttd::Broker::new(config);
    thread::spawn(move || broker.start());
    true
}

/// Connects to the broker on this laptop and forwards every node's telemetry to the UI as
/// "telemetry" events of [topic, payload]. Reconnects by itself.
fn connect(app: AppHandle) -> Client {
    let mut opts = MqttOptions::new(format!("studysphere-desktop-{}", std::process::id()), "127.0.0.1", PORT);
    opts.set_keep_alive(Duration::from_secs(30));
    let (client, mut connection) = Client::new(opts, 10);
    let subscriber = client.clone();
    let events = app.clone();
    let set_connected = move |up: bool| {
        if CONNECTED.swap(up, Ordering::Relaxed) != up {
            let _ = events.emit("connected", up);
        }
    };
    thread::spawn(move || {
        for event in connection.iter() {
            match event {
                Ok(Event::Incoming(Packet::ConnAck(_))) => {
                    // Topics are TOPIC_ROOT/DEVICE_ID/telemetry (firmware/config.h).
                    let _ = subscriber.try_subscribe("+/+/telemetry", QoS::AtMostOnce);
                    set_connected(true);
                }
                Ok(Event::Incoming(Packet::Publish(p))) => {
                    let _ = app.emit("telemetry", (p.topic, String::from_utf8_lossy(&p.payload)));
                }
                Err(_) => {
                    set_connected(false);
                    thread::sleep(Duration::from_secs(1));  // the next iteration reconnects
                }
                _ => {}
            }
        }
    });
    client
}

#[tauri::command]
fn info(mqtt: State<Mqtt>) -> Value {
    // This laptop's addresses, for MQTT_HOST on a real ESP32 (it needs the one on its WiFi).
    let addresses: Vec<Value> = if_addrs::get_if_addrs()
        .unwrap_or_default()
        .into_iter()
        .filter(|i| !i.is_loopback() && i.ip().is_ipv4())
        .map(|i| json!({ "name": i.name, "ip": i.ip().to_string() }))
        .collect();
    json!({
        "ownBroker": mqtt.own_broker,
        "connected": CONNECTED.load(Ordering::Relaxed),
        "port": PORT,
        "addresses": addresses,
    })
}

#[tauri::command]
fn publish(mqtt: State<Mqtt>, topic: String, payload: String) -> Result<(), String> {
    mqtt.client.try_publish(topic, QoS::AtMostOnce, false, payload).map_err(|e| e.to_string())
}

/// The window is always exactly the island's size (logical px), so it never covers more of the
/// screen than the island does, and hangs from the top centre of the main display.
#[tauri::command]
fn resize(window: WebviewWindow, w: f64, h: f64) {
    #[cfg(target_os = "linux")]
    if LAYER.load(Ordering::Relaxed) {
        // A layer surface is as big as GTK asks for (tao's resize doesn't reach it), and the
        // compositor keeps it at the top centre. Runs on the main thread, like every sync command.
        use gtk::prelude::*;
        if let Ok(gw) = window.gtk_window() {
            let (w, h) = (w.round() as i32, h.round() as i32);
            gw.set_size_request(w, h);
            gw.resize(w, h);
        }
        let _ = window.show();
        return;
    }
    // GTK never shrinks a non-resizable window below its natural size, and tao re-applies
    // `resizable: false` after the first configure, so ask every time.
    #[cfg(target_os = "linux")]
    let _ = window.set_resizable(true);
    let _ = window.set_size(LogicalSize::new(w, h));
    // ponytail: main display only, placed on each resize; watch for display changes if needed.
    if let Ok(Some(m)) = window.primary_monitor() {
        let width = (w * m.scale_factor()).round() as i32;
        let x = m.position().x + (m.size().width as i32 - width) / 2;
        let _ = window.set_position(PhysicalPosition::new(x, m.position().y));
    }
    let _ = window.show();
}

#[tauri::command]
fn quit(app: AppHandle) {
    app.exit(0);
}

/// Makes the island an overlay on the top edge where the compositor has layer-shell (KDE,
/// Hyprland, Sway, COSMIC; not GNOME). Elsewhere it stays an always-on-top window that places
/// itself, which on Wayland without layer-shell is up to the compositor.
#[cfg(target_os = "linux")]
fn layer_shell(window: &WebviewWindow) -> bool {
    use gtk::glib::translate::ToGlibPtr;
    use gtk::prelude::*;
    use libloading::{Library, Symbol};
    use std::os::raw::c_int;
    type Win = *mut gtk::ffi::GtkWindow;

    let Ok(gw) = window.gtk_window() else { return false };
    let load = || -> Result<bool, libloading::Error> {
        unsafe {
            let lib = Library::new("libgtk-layer-shell.so.0")?;
            let supported: Symbol<unsafe extern "C" fn() -> c_int> = lib.get(b"gtk_layer_is_supported")?;
            if supported() == 0 || gw.is_realized() {
                return Ok(false);
            }
            let init: Symbol<unsafe extern "C" fn(Win)> = lib.get(b"gtk_layer_init_for_window")?;
            let layer: Symbol<unsafe extern "C" fn(Win, c_int)> = lib.get(b"gtk_layer_set_layer")?;
            let anchor: Symbol<unsafe extern "C" fn(Win, c_int, c_int)> = lib.get(b"gtk_layer_set_anchor")?;
            let zone: Symbol<unsafe extern "C" fn(Win, c_int)> = lib.get(b"gtk_layer_set_exclusive_zone")?;
            let keyboard: Symbol<unsafe extern "C" fn(Win, c_int)> = lib.get(b"gtk_layer_set_keyboard_mode")?;
            // tao gives undecorated Wayland windows an empty titlebar; a layer surface has none.
            gw.set_titlebar(None::<&gtk::Widget>);
            let w: &gtk::Window = gw.upcast_ref();
            let ptr: Win = w.to_glib_none().0;
            init(ptr);
            layer(ptr, 3); // overlay, above other windows and panels
            anchor(ptr, 2, 1); // top edge only: the compositor centres it horizontally
            zone(ptr, -1); // right against the screen edge, over any top bar
            keyboard(ptr, 2); // on demand: the LCD message box can be typed in
            std::mem::forget(lib); // the surface uses it for as long as the app runs
            // WebKitGTK never paints the first frame of a fresh layer surface; unmapping and
            // mapping it once, right after the first map, gets it drawing for good.
            let remapped = std::cell::Cell::new(false);
            gw.connect_map_event(move |w, _| {
                if !remapped.replace(true) {
                    let w = w.clone();
                    gtk::glib::idle_add_local_once(move || {
                        w.hide();
                        w.show_all();
                    });
                }
                gtk::glib::Propagation::Proceed
            });
            Ok(true)
        }
    };
    load().unwrap_or(false)
}

/// WebKitGTK turns the pointer leaving the window into one last move at its edge, so the page
/// never sees `mouseleave` and the island would never tuck away: tell the page from GTK.
#[cfg(target_os = "linux")]
fn watch_pointer_leaving(window: &WebviewWindow) {
    use gtk::prelude::*;
    let Ok(gw) = window.gtk_window() else { return };
    gw.add_events(gtk::gdk::EventMask::LEAVE_NOTIFY_MASK);
    let page = window.clone();
    gw.connect_leave_notify_event(move |_, e| {
        if e.detail() != gtk::gdk::NotifyType::Inferior {
            let _ = page.emit("pointer-left", ());
        }
        gtk::glib::Propagation::Proceed
    });
}

fn main() {
    let own_broker = start_broker();
    tauri::Builder::default()
        .setup(move |app| {
            let client = connect(app.handle().clone());
            app.manage(Mqtt { client, own_broker });
            // Before the window is first shown: a shown window can't become a layer surface.
            #[cfg(target_os = "linux")]
            if let Some(window) = app.get_webview_window("island") {
                LAYER.store(layer_shell(&window), Ordering::Relaxed);
                watch_pointer_leaving(&window);
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![info, publish, resize, quit])
        .run(tauri::generate_context!())
        .expect("error while running StudySphere");
}
