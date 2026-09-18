#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arranca el servidor web HTTP en el puerto 80 y registra los endpoints /, /style.css, /save y /scan.
 * 
 * @return httpd_handle_t Manejador del servidor HTTP iniciado, o NULL si hubo error.
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Detiene el servidor web HTTP y libera recursos.
 * 
 * @param[in] server Manejador del servidor a detener.
 */
void stop_webserver(httpd_handle_t server);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H