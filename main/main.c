#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_wrapper.h"

#define JSON_HEADERS "Content-Type: application/json\r\n"
#define JSON_MAX_SIZE 512
#define SDK_VOID
#ifndef RETURN_IF
#define RETURN_IF(COND, RC, DO) \
  if (COND) {                   \
    DO return RC;               \
  }
#endif

static struct mg_rpc* s_rpc_head = NULL;
static const char* s_http_url = "http://0.0.0.0:8000";
static const char* s_https_url = "https://0.0.0.0:8443";
static const char* s_web_root = "/web_root/";
static const char* s_ca_path = "ca.pem";
static const char* s_cert_path = "cert.pem";
static const char* s_key_path = "key.pem";
static struct mg_str s_ca, s_cert, s_key;

// Authenticated user.
// A user can be authenticated by:
//   - a name:pass pair, passed in a header Authorization: Basic .....
//   - an access_token, passed in a header Cookie: access_token=....
// When a user is shown a login screen, she enters a user:pass. If successful,
// a server responds with a http-only access_token cookie set.
struct user {
  const char *name, *pass, *access_token;
};

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

static void rest_system_handler(struct mg_connection* c, struct mg_http_message* hm,
                         struct mg_str func) {
  if (mg_match(func, mg_str("info"), NULL)) {
    rest_call(c, hm, wrap_sys_info);
  } else {
    mg_http_reply(c, 400, "", "{\"cause\": \"the rest api is not exist\"}\n");
  }
}

static void rest_login_handler(struct mg_connection *c, struct user *u) {
  char cookie[256];
  mg_snprintf(cookie, sizeof(cookie),
              "Set-Cookie: access_token=%s; Path=/; "
              "%sHttpOnly; SameSite=Lax; Max-Age=%d\r\n",
              u->access_token, c->is_tls ? "Secure; " : "", 3600 * 24);
  mg_http_reply(c, 200, cookie, "{%m:%m}", MG_ESC("user"), MG_ESC(u->name));
}

static void rest_logout_handler(struct mg_connection *c) {
  char cookie[256];
  mg_snprintf(cookie, sizeof(cookie),
              "Set-Cookie: access_token=; Path=/; "
              "Expires=Thu, 01 Jan 1970 00:00:00 UTC; "
              "%sHttpOnly; Max-Age=0; \r\n",
              c->is_tls ? "Secure; " : "");
  mg_http_reply(c, 200, cookie, "true\n");
}

static void rest_wifi_handler(struct mg_connection* c,
                             struct mg_http_message* hm, struct mg_str func) {
  if (mg_match(func, mg_str("scan"), NULL)) {
    rest_call(c, hm, wrap_wifi_scan);
  } else if (mg_match(func, mg_str("connect"), NULL)) {
    rest_call(c, hm, wrap_wifi_connect);
  } else {
    mg_http_reply(c, 400, "", "{\"cause\": \"the rest api is not exist\"}\n");
  }
}

// Parse HTTP requests, return authenticated user or NULL
static struct user *authenticate(struct mg_http_message *hm) {
  // In production, make passwords strong and tokens randomly generated
  // In this example, user list is kept in RAM. In production, it can
  // be backed by file, database, or some other method.
  static struct user users[] = {
      {"admin", "admin", "admin_token"},
      {"user1", "user1", "user1_token"},
      {"user2", "user2", "user2_token"},
      {NULL, NULL, NULL},
  };
  char user[64], pass[64];
  struct user *u, *result = NULL;
  mg_http_creds(hm, user, sizeof(user), pass, sizeof(pass));
  MG_INFO(("user [%s] pass [%s]", user, pass));

  if (user[0] != '\0' && pass[0] != '\0') {
    // Both user and password is set, search by user/password
    for (u = users; result == NULL && u->name != NULL; u++)
      if (strcmp(user, u->name) == 0 && strcmp(pass, u->pass) == 0) result = u;
  } else if (user[0] == '\0') {
    // Only password is set, search by token
    for (u = users; result == NULL && u->name != NULL; u++)
      if (strcmp(pass, u->access_token) == 0) result = u;
  }
  return result;
} 

static void rest_handler(struct mg_connection* c, struct mg_http_message* hm,
                         struct mg_str func) {
  printf("%s %.*s\n", __func__, func.len, func.buf);
  struct mg_str caps[2];
  struct user *u = authenticate(hm);
  if (u == NULL) {
    mg_http_reply(c, 403, "", "Not Authorised\n");
  } else if (mg_match(func, mg_str("gpio/*"), caps)) {
    rest_gpio_handler(c, hm, caps[0]);
  } else if (mg_match(func, mg_str("pwm/*"), caps)) {
    rest_pwm_handler(c, hm, caps[0]);
  } else if (mg_match(func, mg_str("sys/*"), caps)) {
    rest_system_handler(c, hm, caps[0]);
  } else if (mg_match(func, mg_str("login"), NULL)) {
    rest_login_handler(c, u);
  } else if (mg_match(func, mg_str("logout"), NULL)) {
    rest_logout_handler(c);
  } else if (mg_match(func, mg_str("wifi/*"), caps)) {
    rest_wifi_handler(c, hm, caps[0]);
  } else {
    mg_http_reply(c, 400, "", "%s", JSON_INVALID_API);
  }
}

static void fn(struct mg_connection* c, int ev, void* ev_data) {
  if (ev == MG_EV_OPEN) {
    // c->is_hexdumping = 1;
  } else if (ev == MG_EV_ACCEPT) {
    if (c->is_tls) {  // TLS listener!
      struct mg_tls_opts opts = {0};
      opts.cert = mg_unpacked("/certs/server_cert.pem");
      opts.key = mg_unpacked("/certs/server_key.pem");
      mg_tls_init(c, &opts);
    }         
  }else if (c->is_tls && ev == MG_EV_ACCEPT) {
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
      struct mg_http_serve_opts opts = {
        .root_dir = s_web_root,
        .fs = &mg_fs_packed,
      };
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
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  wifi_init();

  struct mg_mgr mgr;
  mg_mgr_init(&mgr);

  //mg_timer_add(&mgr, 5000, MG_TIMER_REPEAT, timer_fn, &mgr);  // Init timer
  mg_rpc_add(&s_rpc_head, mg_str("gpio_config"), rpc_gpio_config, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("gpio_info"), rpc_gpio_info, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("gpio_mode"), rpc_gpio_mode, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("gpio_level"), rpc_gpio_level, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("pwm_config"), rpc_pwm_config, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("pwm_duty"), rpc_pwm_duty, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("pwm_stop"), rpc_pwm_stop, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("sys_info"), rpc_sys_info, NULL);
  mg_rpc_add(&s_rpc_head, mg_str("rpc.list"), mg_rpc_list, &s_rpc_head);

  MG_INFO(("Starting http listener on %s", s_http_url));
  MG_INFO(("Starting https listener on %s", s_https_url));

  mg_http_listen(&mgr, s_http_url, fn, NULL);
  mg_http_listen(&mgr, s_https_url, fn, NULL);
  for (;;)
    mg_mgr_poll(&mgr, 10);
  mg_mgr_free(&mgr);
  mg_rpc_del(&s_rpc_head, NULL);
}
