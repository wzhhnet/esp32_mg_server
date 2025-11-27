#include "driver/gpio.h"
#include "esp_spiffs.h"
#include "esp_wrapper.h"
#include "mongoose.h"

#define WIFI_SSID "XiaoSongHome"
#define WIFI_PASS "happyflamingo657"
#define JSON_HEADERS "Content-Type: application/json\r\n"
#define JSON_MAX_SIZE 256
#define SDK_VOID
#ifndef RETURN_IF
#define RETURN_IF(COND, RC, DO) \
  if (COND) {                   \
    DO return RC;               \
  }
#endif
extern void wifi_init(const char* ssid, const char* pass);
static struct mg_rpc* s_rpc_head = NULL;
static const char* s_listen_on = "ws://0.0.0.0:8000";
static const char* s_web_root = ".";
static const char* s_ca_path = "ca.pem";
static const char* s_cert_path = "cert.pem";
static const char* s_key_path = "key.pem";
struct mg_str s_ca, s_cert, s_key;

static void rpc_call(struct mg_rpc_req* r, wrap_func func) {
  char buf[JSON_MAX_SIZE] = {};
  struct mg_str out = {.buf = buf, .len = sizeof(buf)};
  struct mg_str params = mg_json_get_tok(r->frame, "$.params");
  if (!params.buf) {
    mg_rpc_err(r, -32602, "Invalid method parameter(s).");
    return;
  }
  if (!func(params, &out)) {
    mg_rpc_err(r, -32602, "Invalid method parameter(s).");
    return;
  }
  mg_rpc_ok(r, "%.*s", out.len, out.buf);
}

static void rpc_gpio_config(struct mg_rpc_req* r) {
  rpc_call(r, wrap_gpio_config);
}

static void rpc_gpio_info(struct mg_rpc_req* r) {
  rpc_call(r, wrap_gpio_info);
}

static void rpc_gpio_mode(struct mg_rpc_req* r) {
  rpc_call(r, wrap_gpio_mode);
}

static void rpc_gpio_level(struct mg_rpc_req* r) {
  rpc_call(r, wrap_gpio_level);
}

static void rpc_pwm_config(struct mg_rpc_req* r) {
  rpc_call(r, wrap_pwm_config);
}

static void rpc_pwm_duty(struct mg_rpc_req* r) {
  rpc_call(r, wrap_pwm_set_duty);
}

static void rpc_pwm_stop(struct mg_rpc_req* r) {
  rpc_call(r, wrap_pwm_stop);
}

static void rpc_sys_info(struct mg_rpc_req* r) {
  rpc_call(r, wrap_sys_info);
}

static void rest_call(struct mg_connection* c, struct mg_http_message* hm,
                      wrap_func func) {
  char buf[JSON_MAX_SIZE] = {};
  struct mg_str out = {.buf = buf, .len = sizeof(buf)};
  if (func(hm->body, &out)) {
    mg_http_reply(c, 200, "", "%.*s", out.len, out.buf);
  } else {
    mg_http_reply(c, 400, "", "%.*s", out.len, out.buf);
  }
}

static void rest_gpio_handler(struct mg_connection* c,
                              struct mg_http_message* hm, struct mg_str func) {
  if (mg_match(func, mg_str("cfg"), NULL)) {
    rest_call(c, hm, wrap_gpio_config);
  } else if (mg_match(func, mg_str("info"), NULL)) {
    rest_call(c, hm, wrap_gpio_info);
  } else if (mg_match(func, mg_str("mode"), NULL)) {
    rest_call(c, hm, wrap_gpio_mode);
  } else if (mg_match(func, mg_str("level"), NULL)) {
    rest_call(c, hm, wrap_gpio_level);
  } else {
    mg_http_reply(c, 400, "", "%s", JSON_INVALID_API);
  }
}

static void rest_pwm_handler(struct mg_connection* c,
                             struct mg_http_message* hm, struct mg_str func) {
  if (mg_match(func, mg_str("cfg"), NULL)) {
    rest_call(c, hm, wrap_pwm_config);
  } else if (mg_match(func, mg_str("duty"), NULL)) {
    rest_call(c, hm, wrap_pwm_set_duty);
  } else if (mg_match(func, mg_str("stop"), NULL)) {
    rest_call(c, hm, wrap_pwm_stop);
  } else {
    mg_http_reply(c, 400, "", "{\"cause\": \"the rest api is not exist\"}\n");
  }
}

void rest_system_handler(struct mg_connection* c, struct mg_http_message* hm,
                         struct mg_str func) {
  if (mg_match(func, mg_str("info"), NULL)) {
    rest_call(c, hm, wrap_sys_info);
  } else {
    mg_http_reply(c, 400, "", "{\"cause\": \"the rest api is not exist\"}\n");
  }
}

static void rest_handler(struct mg_connection* c, struct mg_http_message* hm,
                         struct mg_str func) {
  printf("%s %.*s\n", __func__, func.len, func.buf);
  struct mg_str caps[2];
  if (mg_match(func, mg_str("gpio/*"), caps)) {
    rest_gpio_handler(c, hm, caps[0]);
  } else if (mg_match(func, mg_str("pwm/*"), caps)) {
    rest_pwm_handler(c, hm, caps[0]);
  } else if (mg_match(func, mg_str("sys/*"), caps)) {
    rest_system_handler(c, hm, caps[0]);
  }
}

static void fn(struct mg_connection* c, int ev, void* ev_data) {
  if (ev == MG_EV_OPEN) {
    // c->is_hexdumping = 1;
  } else if (c->is_tls && ev == MG_EV_ACCEPT) {
    s_ca = mg_file_read(&mg_fs_posix, s_ca_path);
    s_cert = mg_file_read(&mg_fs_posix, s_cert_path);
    s_key = mg_file_read(&mg_fs_posix, s_key_path);
    struct mg_tls_opts opts = {.ca = s_ca, .cert = s_cert, .key = s_key};
    mg_tls_init(c, &opts);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_str caps[2];
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;
    if (mg_match(hm->uri, mg_str("/websocket"), NULL)) {
      mg_ws_upgrade(c, hm, NULL);
    } else if (mg_match(hm->uri, mg_str("/rest/#"), caps)) {
      rest_handler(c, hm, caps[0]);
    } else {
      // Serve static files
      struct mg_http_serve_opts opts = {.root_dir = s_web_root};
      mg_http_serve_dir(c, ev_data, &opts);
    }
  } else if (ev == MG_EV_WS_MSG) {
    struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
    struct mg_iobuf io = {0, 0, 0, 512};
    struct mg_rpc_req r = {&s_rpc_head, 0, mg_pfn_iobuf, &io, 0, wm->data};
    mg_rpc_process(&r);
    if (io.buf)
      mg_ws_send(c, (char*)io.buf, io.len, WEBSOCKET_OP_TEXT);
    mg_iobuf_free(&io);
  } else if (ev == MG_EV_CLOSE && c->is_tls) {
    mg_free(s_ca.buf);
    mg_free(s_cert.buf);
    mg_free(s_key.buf);
  }
}

static void timer_fn(void *arg) {
  struct mg_mgr *mgr = (struct mg_mgr *) arg;
  // Broadcast message to all connected websocket clients.
  for (struct mg_connection *c = mgr->conns; c != NULL; c = c->next) {
    //if (c->data[0] != 'W') continue;
    mg_ws_printf(c, WEBSOCKET_OP_TEXT, "{%m:%m,%m:{}}",
                 MG_ESC("method"), MG_ESC("keepalive"), MG_ESC("params"));
    MG_INFO(("keepalive"));
  }
}

void app_main() {
  esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .max_files = 20,
      .format_if_mount_failed = true,
  };
  esp_vfs_spiffs_register(&conf);

  wifi_init(WIFI_SSID, WIFI_PASS);  // This blocks until connected
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_timer_add(&mgr, 5000, MG_TIMER_REPEAT, timer_fn, &mgr);  // Init timer
  mg_rpc_add(&s_rpc_head, mg_str("gpio_config"), rpc_gpio_config, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("gpio_info"), rpc_gpio_info, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("gpio_mode"), rpc_gpio_mode, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("gpio_level"), rpc_gpio_level, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("pwm_config"), rpc_pwm_config, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("pwm_duty"), rpc_pwm_duty, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("pwm_stop"), rpc_pwm_stop, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("sys_info"), rpc_sys_info, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("rpc.list"), mg_rpc_list, &s_rpc_head);

  printf("Starting WS listener on %s/websocket\n", s_listen_on);
  mg_http_listen(&mgr, s_listen_on, fn, NULL);
  for (;;)
    mg_mgr_poll(&mgr, 10);
  mg_mgr_free(&mgr);
  mg_rpc_del(&s_rpc_head, NULL);
}
