const form    = document.getElementById('wifiForm');
const message = document.getElementById('message');

form.addEventListener('submit', function (e) {
    e.preventDefault();   // evita que la página recargue sola

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
            message.textContent = 'Credenciales guardadas. Intentando conectar...';
            message.className   = 'ok';
        } else {
            message.textContent = 'Error al guardar. Intentalo de nuevo.';
            message.className   = 'err';
        }
    })
    .catch(function () {
        // El ESP32 puede cortar la conexion al reconectarse — igual funciono
        message.textContent = 'Credenciales enviadas. Esperando conexion...';
        message.className   = 'ok';
    });
});
