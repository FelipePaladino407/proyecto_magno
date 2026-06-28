const form = document.getElementById("configForm");
const message = document.getElementById("message");

form.addEventListener("submit", function (e) {
  e.preventDefault(); // evita que la página recargue sola

  message.textContent = "Guardando...";
  message.className = "";

  const data = new URLSearchParams(new FormData(form));

  fetch("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: data.toString(),
  })
    .then(function (res) {
      if (res.ok) {
        message.textContent = "Configuración guardada. Intentando conectar...";
        message.className = "ok";
      } else {
        message.textContent = "Error al guardar. Intentalo de nuevo.";
        message.className = "err";
      }
    })
    .catch(function () {
      // El ESP32 puede cortar la conexion al reconectarse — igual funciono
      message.textContent = "Configuración enviada. Esperando conexion...";
      message.className = "ok";
    });
});
