const dot        = document.getElementById('statusDot');
const statusText = document.getElementById('statusText');
const message    = document.getElementById('message');

/* ---- Helpers ---- */
function setStatus(state, text) {
    dot.className    = 'status-dot ' + (state || '');
    statusText.textContent = text;
}

function setMessage(text, type) {
    message.textContent = text;
    message.className   = type || '';
}

/* ---- Consultar estado actual al cargar la página ----
   Requiere GET /api/status en el backend.
   Si no está disponible todavía, falla silenciosamente. */
function fetchStatus() {
    fetch('/api/status')
        .then(function(res) { return res.json(); })
        .then(function(data) {
            if (data.connected) {
                setStatus('connected', 'Conectado · ' + data.ip_address);
            } else if (data.credentials_saved) {
                setStatus('connecting', 'Conectando a ' + data.sta_ssid + '...');
            } else {
                setStatus('', 'Sin conexion');
            }
        })
        .catch(function() {
            /* /api/status todavia no implementado — ignorar */
        });
}

fetchStatus();

/* ---- Guardar credenciales ---- */
document.getElementById('btnGuardar').addEventListener('click', function() {
    var ssid = document.getElementById('ssid').value.trim();
    var pass = document.getElementById('password').value;

    if (!ssid) {
        setMessage('Ingresa el nombre de la red.', 'err');
        return;
    }

    setMessage('Guardando...', '');
    setStatus('connecting', 'Conectando...');

    var body = 'ssid=' + encodeURIComponent(ssid) +
               '&password=' + encodeURIComponent(pass);

    fetch('/api/wifi', {
        method:  'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body:    body
    })
    .then(function(res) {
        if (res.ok) {
            setMessage('Credenciales guardadas. Esperando conexion...', 'ok');
            setStatus('connecting', 'Conectando a ' + ssid + '...');
        } else {
            setMessage('Error al guardar. Intentalo de nuevo.', 'err');
            setStatus('', 'Sin conexion');
        }
    })
    .catch(function() {
        /* El ESP32 puede cortar la conexion al reconectarse — igual funciono */
        setMessage('Credenciales enviadas. Esperando conexion...', 'ok');
        setStatus('connecting', 'Conectando a ' + ssid + '...');
    });
});

/* ---- Olvidar red ----
   Requiere POST /api/wifi/clear en el backend. */
document.getElementById('btnOlvidar').addEventListener('click', function() {
    setMessage('Olvidando red...', '');

    fetch('/api/wifi/clear', { method: 'POST' })
        .then(function(res) {
            if (res.ok) {
                setMessage('Red olvidada.', 'ok');
                setStatus('', 'Sin conexion');
            } else {
                setMessage('Error al olvidar la red.', 'err');
            }
        })
        .catch(function() {
            setMessage('Error de conexion.', 'err');
        });
});
