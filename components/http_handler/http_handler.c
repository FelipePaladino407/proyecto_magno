#include "http_handler.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "wifi_manager.h"

// TAG para logs
static const char *TAG = "http_handler";
static httpd_handle_t s_server = NULL;

/* Archivos embedidos por el build system (EMBED_TXTFILES en CMakeLists.txt) */
extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[]   asm("_binary_index_html_end");

extern const unsigned char style_css_start[]  asm("_binary_style_css_start");
extern const unsigned char style_css_end[]    asm("_binary_style_css_end");

extern const unsigned char app_js_start[]     asm("_binary_app_js_start");
extern const unsigned char app_js_end[]       asm("_binary_app_js_end");

// Helper para servir un archivo embedido
static esp_err_t send_embedded(httpd_req_t *req, const char *content_type,
                                const unsigned char *start,
                                const unsigned char *end)
{
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)start,
                           (ssize_t)(end - start));
}

// Handler para servir el archivo HTML embebido.
static esp_err_t index_handler(httpd_req_t *req)
{
    return send_embedded(req, "text/html; charset=utf-8",
                         index_html_start, index_html_end);
}

// Handler para servir el archivo CSS embebido.
static esp_err_t style_handler(httpd_req_t *req)
{
    return send_embedded(req, "text/css; charset=utf-8",
                         style_css_start, style_css_end);
}

// Handler para servir el archivo JavaScript embebido.
static esp_err_t js_handler(httpd_req_t *req)
{
    return send_embedded(req, "application/javascript; charset=utf-8",
                         app_js_start, app_js_end);
}

/* --- URL decode --- */
static bool is_hex(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

// Convierte un caracter hexadecimal a su valor numérico. 
// Asume que c es un caracter hexadecimal válido.
static unsigned char hex_val(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned char)(c - 'a' + 10);
    return (unsigned char)(c - 'A' + 10);
}

// Decodifica una cadena URL-encoded. 
//Devuelve true si tuvo exito o false si hubo un error (e.g. buffer de destino muy chico).
static bool url_decode(const char *src, char *dst, size_t dst_size)
{
    size_t r = 0, w = 0;
    while (src[r] != '\0') {
        if (w + 1 >= dst_size) return false;
        if (src[r] == '+') {
            dst[w++] = ' '; r++;
        } else if (src[r] == '%' && is_hex(src[r+1]) && is_hex(src[r+2])) {
            dst[w++] = (char)((hex_val(src[r+1]) << 4) | hex_val(src[r+2]));
            r += 3;
        } else {
            dst[w++] = src[r++];
        }
    }
    dst[w] = '\0';
    return true;
}

// POST /api/wifi — recibe ssid y password del formulario
static esp_err_t wifi_config_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 512) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "Solicitud invalida");
    }

    char *body = calloc((size_t)req->content_len + 1, 1);
    if (!body) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "Sin memoria");
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received,
                               (size_t)(req->content_len - received));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { free(body); return ESP_FAIL; }
        received += r;
    }

    char enc_ssid[129] = {0};
    char enc_pass[257] = {0};
    char ssid[33]      = {0};
    char password[64]  = {0};

    esp_err_t r_ssid = httpd_query_key_value(body, "ssid",
                                              enc_ssid, sizeof(enc_ssid));
    esp_err_t r_pass = httpd_query_key_value(body, "password",
                                              enc_pass, sizeof(enc_pass));
    free(body);

    if (r_ssid != ESP_OK ||
        !url_decode(enc_ssid, ssid, sizeof(ssid)) ||
        !url_decode(enc_pass, password, sizeof(password))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "SSID o contrasena invalidos");
    }

    if (r_pass != ESP_OK) password[0] = '\0';

    esp_err_t err = wifi_manager_set_credentials(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "No se pudo guardar la configuracion");
    }

    httpd_resp_set_status(req, "200 OK");
    return httpd_resp_sendstr(req, "OK");
}

// Inicia el servidor web y registra los handlers. 
// Devuelve ESP_OK si tuvo exito o un error de lo contrario.
esp_err_t http_handler_start(void)
{
    if (s_server != NULL) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 6;
    config.stack_size = 6144;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar el servidor: %s",
                 esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = index_handler
    };
    const httpd_uri_t uri_index_html = {
        .uri = "/index.html", .method = HTTP_GET, .handler = index_handler
    };
    const httpd_uri_t uri_css = {
        .uri = "/style.css", .method = HTTP_GET, .handler = style_handler
    };
    const httpd_uri_t uri_js = {
        .uri = "/app.js", .method = HTTP_GET, .handler = js_handler
    };
    const httpd_uri_t uri_wifi = {
        .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_config_handler
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_index));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_index_html));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_css));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_js));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_wifi));

    ESP_LOGI(TAG, "Servidor iniciado en http://192.168.4.1");
    return ESP_OK;
}

// Detiene el servidor web si estaba corriendo.
esp_err_t http_handler_stop(void)
{
    if (s_server == NULL) return ESP_OK;
    esp_err_t err = httpd_stop(s_server);
    if (err == ESP_OK) s_server = NULL;
    return err;
}
