const form    = document.getElementById('wifiForm');
const message = document.getElementById('message');

function checkStatus(attempts) {
    if (attempts <= 0) {
        message.textContent = 'No se pudo verificar la conexión. Revisá la red y la contraseña.';
        message.className   = 'err';
        return;
    }

    fetch('/api/status')
        .then(function (res) { return res.json(); })
        .then(function (data) {
            if (data.connected) {
                message.textContent = '✓ Conectado a ' + data.ssid + ' — IP: ' + data.ip;
                message.className   = 'ok';
            } else {
                // Todavía no conectó, reintentamos en 2 segundos
                setTimeout(function () { checkStatus(attempts - 1); }, 2000);
            }
        })
        .catch(function () {
            // El ESP32 puede estar reconectándose y no responder — reintentamos
            setTimeout(function () { checkStatus(attempts - 1); }, 2000);
        });
}

form.addEventListener('submit', function (e) {
    e.preventDefault();

    message.textContent = 'Guardando...';
    message.className   = '';

    const data = new URLSearchParams(new FormData(form));

    fetch('/api/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: data.toString()
    })
    .then(function (res) {
        if (res.ok) {
            message.textContent = 'Credenciales guardadas. Conectando...';
            message.className   = 'ok';
            // Esperamos 3 segundos para darle tiempo al ESP32 de conectarse
            // y después empezamos a preguntar cada 2 segundos, hasta 10 intentos (20 seg)
            setTimeout(function () { checkStatus(10); }, 3000);
        } else {
            message.textContent = 'Error al guardar. Intentalo de nuevo.';
            message.className   = 'err';
        }
    })
    .catch(function () {
        // El ESP32 cortó la conexión al reconectarse — igual mandamos las credenciales
        message.textContent = 'Credenciales enviadas. Verificando conexión...';
        message.className   = 'ok';
        setTimeout(function () { checkStatus(10); }, 3000);
    });
});
