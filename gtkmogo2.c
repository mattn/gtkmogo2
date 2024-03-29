#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <curl/curl.h>
#include <memory.h>
#include <string.h>
#include <libintl.h>

#ifdef _LIBINTL_H
#include <locale.h>
# define _(x) gettext(x)
#else
# define _(x) x
#endif

#ifdef _WIN32
# define DATA_DIR "data"
# define LOCALE_DIR "share/locale"
# ifndef snprintf
#  define snprintf _snprintf
# endif
#endif

#define APP_TITLE                  "GtkMogo2"
#define APP_NAME                   "gtkmogo2"
#define APP_VERSION                "0.0.2"
#define APP_URL                    "http://www.ac.cyberhome.ne.jp/~mattn/gtkmogo2.xml"
#define SERVICE_NAME               "mogo2"
#define SERVICE_UPDATE_URL         "http://api.mogo2.jp/statuses/update.xml"
#define SERVICE_SELF_STATUS_URL    "http://api.mogo2.jp/statuses/friends_timeline.xml"
#define SERVICE_FRIENDS_STATUS_URL "http://api.mogo2.jp/statuses/friends_timeline/%s.xml"
#define SERVICE_THREAD_STATUS_URL  "http://api.mogo2.jp/statuses/thread_timeline/%s.xml"
#define USE_REPLAY_ACCESS          1
#define TINYURL_API_URL            "http://tinyurl.com/api-create.php"
#define ACCEPT_LETTER_URL          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789;/?:@&=+$,-_.!~*'%"
#define ACCEPT_LETTER_NAME         "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
#define ACCEPT_LETTER_REPLY        "1234567890"
#define RELOAD_TIMER_SPAN          (30*60*1000)

#define XML_CONTENT(x) (x->children ? (char*)x->children->content : NULL)

static GdkCursor* hand_cursor = NULL;
static GdkCursor* regular_cursor = NULL;

typedef struct _PIXBUF_CACHE {
	char* id;
	GdkPixbuf* pixbuf;
} PIXBUF_CACHE;

typedef struct _PROCESS_THREAD_INFO {
	GThreadFunc func;
	gboolean processing;
	gpointer data;
	gpointer retval;
} PROCESS_THREAD_INFO;

/**
 * timer register
 */
static guint timer_tag = 0;
static void start_reload_timer(GtkWidget* toplevel);
static void stop_reload_timer(GtkWidget* toplevel);
static void reset_reload_timer(GtkWidget* toplevel);

static gboolean login_dialog(GtkWidget* window);
static int load_config(GtkWidget* window);
static int save_config(GtkWidget* window);

/**
 * curl callback
 */
static char* response_cond = NULL;	/* response condition */
static char* response_mime = NULL;	/* response content-type. ex: "text/html" */
static char* response_data = NULL;	/* response data from server. */
static size_t response_size = 0;	/* response size of data */
static char last_condition[256] = {0};
static int is_processing = FALSE;

static initialize_http_response() {
	response_cond = NULL;
	response_mime = NULL;
	response_data = NULL;
	response_size = 0;
}

static terminate_http_response() {
	if (response_cond) free(response_cond);
	if (response_mime) free(response_mime);
	if (response_data) free(response_data);
	response_cond = NULL;
	response_mime = NULL;
	response_data = NULL;
	response_size = 0;
}

static time_t strtotime(char *s) {
	char *os;
	int i;
	struct tm tm;
	int isleap;

	static int mday[2][12] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
		31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	};
	static char* wday[] = {
		"Sunday", "Monday", "Tuesday", "Wednesday",
		"Thursday", "Friday", "Saturday",
		NULL
	};
	static char* mon[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
		NULL
	};

	os = s;
	/* Sunday, */
	for(i = 0; i < sizeof(wday)/sizeof(wday[0]); i++) {
		if (strncmp(s, wday[i], strlen(wday[i])) == 0) {
			s += strlen(wday[i]);
			break;
		}
		if (strncmp(s, wday[i], 3) == 0) {
			s += 3;
			break;
		}
	}
	if (i == sizeof(wday)/sizeof(wday[0])) return -1;
	if (*s++ != ',' || *s++ != ' ') return -1;

	/* 25- */
	if (!isdigit(s[0]) || !isdigit(s[1]) || (s[2]!='-' && s[2]!=' ')) return -1;
	tm.tm_mday = strtol(s, 0, 10);
	s += 3;

	/* Jan- */
	for(i = 0; i<sizeof(mon)/sizeof(mon[0]); i++) {
		if (strncmp(s, mon[i], 3) == 0) {
			tm.tm_mon = i;
			s += 3;
			break;
		}
	}
	if (i == sizeof(mon)/sizeof(mon[0])) return -1;
	if (s[0] != '-' && s[0] != ' ') return -1;
	s++;

	/* 2002 */
	if (!isdigit(s[0]) || !isdigit(s[1])) return -1;
	tm.tm_year = strtol(s, 0, 10);
	s += 2;
	if (isdigit(s[0]) && isdigit(s[1]))
		s += 2;
	else {
		tm.tm_year += (tm.tm_year <= 68) ? 2000 : 1900;
	}
	isleap = (tm.tm_year % 4 == 0)
		&& (tm.tm_year % 100 != 0 || tm.tm_year % 400 == 0);
	if (tm.tm_mday == 0 || tm.tm_mday > mday[isleap][tm.tm_mon]) return -1;
	tm.tm_year -= 1900;
	if (*s++ != ' ') return -1;

	if (!isdigit(s[0]) || !isdigit(s[1]) || s[2]!=':'
	 || !isdigit(s[3]) || !isdigit(s[4]) || s[5]!=':'
	 || !isdigit(s[6]) || !isdigit(s[7]) || s[8]!=' ') return -1;

	tm.tm_hour = atoi(s);
	tm.tm_min = atoi(s+3);
	tm.tm_sec = atoi(s+6);
	if (tm.tm_hour >= 24 || tm.tm_min >= 60 || tm.tm_sec >= 60) return -1;
	s += 9;

	if (strncmp(s, "GMT", 3) != 0) return -1;
	tm.tm_yday = 0;
	return mktime(&tm);
}

static size_t handle_returned_data(char* ptr, size_t size, size_t nmemb, void* stream) {
	if (!response_data)
		response_data = (char*)malloc(size*nmemb);
	else
		response_data = (char*)realloc(response_data, response_size+size*nmemb);
	if (response_data) {
		memcpy(response_data+response_size, ptr, size*nmemb);
		response_size += size*nmemb;
	}
	return size*nmemb;
}

static size_t handle_returned_header(void* ptr, size_t size, size_t nmemb, void* stream) {
	char* header = NULL;

	header = malloc(size*nmemb + 1);
	memcpy(header, ptr, size*nmemb);
	header[size*nmemb] = 0;
	if (strncmp(header, "Content-Type: ", 14) == 0) {
		char* stop = header + 14;
		stop = strpbrk(header + 14, "\r\n;");
		if (stop) *stop = 0;
		if (response_mime) free(response_mime);
		response_mime = strdup(header + 14);
	}
	if (strncmp(header, "Last-Modified: ", 15) == 0) {
		char* stop = strpbrk(header, "\r\n;");
		if (stop) *stop = 0;
		if (response_cond) free(response_cond);
		response_cond = strdup(header);
	}
	if (strncmp(header, "ETag: ", 6) == 0) {
		char* stop = strpbrk(header, "\r\n;");
		if (stop) *stop = 0;
		if (response_cond) free(response_cond);
		response_cond = strdup(header);
	}
	free(header);
	return size*nmemb;
}

/**
 * string utilities
 */
static char* xml_decode_alloc(const char* str) {
	char* buf = NULL;
	unsigned char* pbuf = NULL;
	int len = 0;

	if (!str) return NULL;
	len = strlen(str)*3;
	buf = malloc(len+1);
	memset(buf, 0, len+1);
	pbuf = (unsigned char*)buf;
	while(*str) {
		if (!memcmp(str, "&amp;", 5)) {
			strcat((char*)pbuf++, "&");
			str += 5;
		} else
		if (!memcmp(str, "&nbsp;", 6)) {
			strcat((char*)pbuf++, " ");
			str += 6;
		} else
		if (!memcmp(str, "&quot;", 6)) {
			strcat((char*)pbuf++, "\"");
			str += 6;
		} else
		if (!memcmp(str, "&nbsp;", 6)) {
			strcat((char*)pbuf++, " ");
			str += 6;
		} else
		if (!memcmp(str, "&lt;", 4)) {
			strcat((char*)pbuf++, "<");
			str += 4;
		} else
		if (!memcmp(str, "&gt;", 4)) {
			strcat((char*)pbuf++, ">");
			str += 4;
		} else
			*pbuf++ = *str++;
	}
	return buf;
}

static char* get_tiny_url_alloc(const char* url, GError** error) {
	char api_url[2048];
	CURLcode res;
	GError* _error = NULL;
	CURL* curl;
	char* ret = NULL;
	int status = 0;

	snprintf(api_url, sizeof(api_url)-1, "%s/?url=%s", TINYURL_API_URL, url);

	/* initialize callback data */
	initialize_http_response();

	curl = curl_easy_init();
	if (!curl) return NULL;
	curl_easy_setopt(curl, CURLOPT_URL, api_url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handle_returned_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, handle_returned_header);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, APP_NAME);
	res = curl_easy_perform(curl);
	res = res == CURLE_OK ? curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &status) : res;
	curl_easy_cleanup(curl);
	if (res == CURLE_OK && status == 200) {
		ret = malloc(response_size+1);
		memset(ret, 0, response_size+1);
		memcpy(ret, (char*)response_data, response_size);
	}
	else
		_error = g_error_new_literal(G_FILE_ERROR, res, curl_easy_strerror(res));

	/* cleanup callback data */
	terminate_http_response();
	if (error && _error) *error = _error;
	return ret;
}

static char* url_encode_alloc(const char* str) {
	static const int force_encode_all = TRUE;
	const char* hex = "0123456789abcdef";

	char* buf = NULL;
	unsigned char* pbuf = NULL;
	int len = 0;

	if (!str) return NULL;
	len = strlen(str)*3;
	buf = malloc(len+1);
	memset(buf, 0, len+1);
	pbuf = (unsigned char*)buf;
	while(*str) {
		unsigned char c = (unsigned char)*str;
		if (c == ' ')
			*pbuf++ = '+';
		else if (c & 0x80 || force_encode_all) {
			*pbuf++ = '%';
			*pbuf++ = hex[c >> 4];
			*pbuf++ = hex[c & 0x0f];
		} else
			*pbuf++ = c;
		str++;
	}
	return buf;
}

char* sanitize_message_alloc(const char* message) {
	const char* ptr = message;
	const char* last = ptr;
	char* ret = NULL;
	int len = 0;
	while(*ptr) {
		if (!strncmp(ptr, "http://", 7) || !strncmp(ptr, "ftp://", 6)) {
			char* link;
			char* tiny_url;
			const char* tmp;

			if (last != ptr) {
				len += (ptr-last);
				if (!ret) {
					ret = malloc(len+1);
					memset(ret, 0, len+1);
				} else ret = realloc(ret, len+1);
				strncat(ret, last, ptr-last);
			}

			tmp = ptr;
			while(*tmp && strchr(ACCEPT_LETTER_URL, *tmp)) tmp++;
			link = malloc(tmp-ptr+1);
			memset(link, 0, tmp-ptr+1);
			memcpy(link, ptr, tmp-ptr);
			tiny_url = get_tiny_url_alloc(link, NULL);
			if (tiny_url) {
				free(link);
				link = tiny_url;
			}

			len += strlen(link);
			if (!ret) {
				ret = malloc(len+1);
				memset(ret, 0, len+1);
			} else ret = realloc(ret, len+1);
			strcat(ret, link);
			free(link);
			ptr = last = tmp;
		} else
			ptr++;
	}
	if (last != ptr) {
		len += (ptr-last);
		if (!ret) {
			ret = malloc(len+1);
			memset(ret, 0, len+1);
		} else ret = realloc(ret, len+1);
		strncat(ret, last, ptr-last);
	}
	return ret;
}

/**
 * loading icon
 */
static GdkPixbuf* url2pixbuf(const char* url, GError** error) {
	GdkPixbuf* pixbuf = NULL;
	GdkPixbufLoader* loader = NULL;
	GdkPixbufFormat* format = NULL;
	GError* _error = NULL;
	CURL* curl = NULL;
	CURLcode res = CURLE_OK;

	/* initialize callback data */
	initialize_http_response();

	if (!strncmp(url, "file:///", 8) || g_file_test(url, G_FILE_TEST_EXISTS)) {
		gchar* newurl = g_filename_from_uri(url, NULL, NULL);
		pixbuf = gdk_pixbuf_new_from_file(newurl ? newurl : url, &_error);
	} else {
		curl = curl_easy_init();
		if (!curl) return NULL;
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handle_returned_data);
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, handle_returned_header);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, APP_NAME);
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		if (res == CURLE_OK) {
			if (response_mime) loader = gdk_pixbuf_loader_new_with_mime_type(response_mime, error);
			if (!loader) loader = gdk_pixbuf_loader_new();
			if (gdk_pixbuf_loader_write(loader, (const guchar*)response_data, response_size, &_error)) {
				pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
				format = gdk_pixbuf_loader_get_format(loader);
			}
			gdk_pixbuf_loader_close(loader, NULL);
		} else
			_error = g_error_new_literal(G_FILE_ERROR, res, curl_easy_strerror(res));
	}

	/* cleanup callback data */
	terminate_http_response();
	if (error && _error) *error = _error;
	return pixbuf;
}

/**
 * processing message funcs
 */
static gpointer process_thread(gpointer data) {
	PROCESS_THREAD_INFO* info = (PROCESS_THREAD_INFO*)data;

	info->retval = info->func(info->data);
	info->processing = FALSE;

	return info->retval;
}

static gpointer process_func(GThreadFunc func, gpointer data, GtkWidget* parent, const gchar* message) {
	GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget* vbox;
	GtkWidget* image;
	GdkColor color;
	PROCESS_THREAD_INFO info;
	GError *error = NULL;
	GThread* thread;
	GdkCursor* cursor = gdk_cursor_new(GDK_WATCH);

	if (parent)
		parent = gtk_widget_get_toplevel(parent);

	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_widget_hide_on_delete(window);

	vbox = gtk_vbox_new(FALSE, 6);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	image = gtk_image_new_from_file(DATA_DIR"/loading.gif");
	if (image) gtk_container_add(GTK_CONTAINER(vbox), image);

	if (message) {
		GtkWidget* label = gtk_label_new(message);
		gtk_container_add(GTK_CONTAINER(vbox), label);
	}

	gdk_color_parse("#F0F0F0", &color);
	gtk_widget_modify_bg(window, GTK_STATE_NORMAL, &color);

	gtk_widget_queue_resize(window);

	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

	if (parent) {
		gtk_window_set_transient_for(
				GTK_WINDOW(window),
				GTK_WINDOW(parent));
		gdk_window_set_cursor(parent->window, cursor);
	}

	gtk_widget_show_all(window);

	gdk_window_set_cursor(window->window, cursor);
	gdk_flush();
	gdk_cursor_destroy(cursor);

	gdk_threads_leave();

	info.func = func;
	info.data = data;
	info.retval = NULL;
	info.processing = TRUE;
	thread = g_thread_create(
			process_thread,
			&info,
			TRUE,
			&error);
	while(info.processing) {
		gdk_threads_enter();
		while(gtk_events_pending())
			gtk_main_iteration();
		gdk_threads_leave();
		g_thread_yield();
	}
	g_thread_join(thread);

	gdk_threads_enter();
	gtk_widget_hide(window);

	if (parent) {
		gdk_window_set_cursor(parent->window, NULL);
	}
	return info.retval;
}

/**
 * dialog message func
 */
static void error_dialog(GtkWidget* widget, const char* message) {
	GtkWidget* dialog;
	dialog = gtk_message_dialog_new(
			GTK_WINDOW(gtk_widget_get_toplevel(widget)),
			(GtkDialogFlags)(GTK_DIALOG_MODAL),
			GTK_MESSAGE_WARNING,
			GTK_BUTTONS_CLOSE,
			message);
	gtk_window_set_title(GTK_WINDOW(dialog), APP_TITLE);
	gtk_widget_show(dialog);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_window_set_transient_for(
			GTK_WINDOW(dialog),
			GTK_WINDOW(gtk_widget_get_toplevel(widget)));
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static void insert_status_text(GtkTextBuffer* buffer, GtkTextIter* iter, const char* status) {
	char* ptr = (char*)status;
	char* last = ptr;
	if (!status) return;
	while(*ptr) {
		if (!strncmp(ptr, "http://", 7) || !strncmp(ptr, "ftp://", 6)) {
			GtkTextTag *tag;
			int len;
			char* link;
			char* tmp;
			gchar* url;

			if (last != ptr)
				gtk_text_buffer_insert(buffer, iter, last, ptr-last);

			tmp = ptr;
			while(*tmp && strchr(ACCEPT_LETTER_URL, *tmp)) tmp++;
			len = (int)(tmp-ptr);
			link = malloc(len+1);
			memset(link, 0, len+1);
			strncpy(link, ptr, len);
			tag = gtk_text_buffer_create_tag(
					buffer,
					NULL, 
					"foreground",
					"blue", 
					"underline",
					PANGO_UNDERLINE_SINGLE, 
					NULL);
			url = g_strdup(link);
			g_object_set_data(G_OBJECT(tag), "url", (gpointer)url);
			gtk_text_buffer_insert_with_tags(buffer, iter, link, -1, tag, NULL);
			free(link);
			ptr = last = tmp;
		} else
		if (*ptr == '@' || !strncmp(ptr, "\xef\xbc\xa0", 3)) {
			GtkTextTag *tag;
			int len;
			char* link;
			char* tmp;
			gchar* url;
			gchar* user_id;
			gchar* user_name;

			if (last != ptr)
				gtk_text_buffer_insert(buffer, iter, last, ptr-last);

			user_name = tmp = ptr + (*ptr == '@' ? 1 : 3);
			while(*tmp && strchr(ACCEPT_LETTER_NAME, *tmp)) tmp++;
			len = (int)(tmp-user_name);
			if (len) {
				link = malloc(len+1);
				memset(link, 0, len+1);
				strncpy(link, user_name, len);
				url = g_strdup_printf("@%s", link);
				user_id = g_strdup(link);
				user_name = g_strdup(link);
				free(link);
				tag = gtk_text_buffer_create_tag(
						buffer,
						NULL, 
						"foreground",
						"blue", 
						"underline",
						PANGO_UNDERLINE_SINGLE, 
						NULL);
				g_object_set_data(G_OBJECT(tag), "user_id", (gpointer)user_id);
				g_object_set_data(G_OBJECT(tag), "user_name", (gpointer)user_name);
				gtk_text_buffer_insert_with_tags(buffer, iter, url, -1, tag, NULL);
				g_free(url);
				ptr = last = tmp;
			} else
				ptr = tmp;
		} else
#ifdef USE_REPLAY_ACCESS
		if (!strncmp(ptr, ">>", 2)) {
			GtkTextTag *tag;
			int len;
			char* link;
			char* tmp;
			gchar* url;

			if (last != ptr)
				gtk_text_buffer_insert(buffer, iter, last, ptr-last);

			url = tmp = ptr + 2;
			while(*tmp && strchr(ACCEPT_LETTER_REPLY, *tmp)) tmp++;
			len = (int)(tmp-url);
			if (len) {
				link = malloc(len+1);
				memset(link, 0, len+1);
				strncpy(link, url, len);
				url = g_strdup_printf(">>%s", link);
				free(link);
				tag = gtk_text_buffer_create_tag(
						buffer,
						NULL, 
						"foreground",
						"blue", 
						"underline",
						PANGO_UNDERLINE_SINGLE, 
						NULL);
				g_object_set_data(G_OBJECT(tag), "url", (gpointer)url);
				gtk_text_buffer_insert_with_tags(buffer, iter, url, -1, tag, NULL);
				ptr = last = tmp;
			} else
				ptr = tmp;
		} else
#endif
			ptr++;
	}
	if (last != ptr)
		gtk_text_buffer_insert(buffer, iter, last, ptr-last);
}

/**
 * update friends statuses
 */
static gpointer update_friends_statuses_thread(gpointer data) {
	GtkWidget* window = (GtkWidget*)data;
	GtkTextBuffer* buffer = NULL;
	GtkTextTag* name_tag = NULL;
	GtkTextTag* date_tag = NULL;
	CURL* curl = NULL;
	CURLcode res = CURLE_OK;
	struct curl_slist *headers = NULL;
	int status = 0;
	gchar* user_id = NULL;
	gchar* user_name = NULL;
	gchar* status_id = NULL;
	gchar* title = NULL;

	char url[2048];
	char auth[512];
	char* recv_data = NULL;
	char* mail = NULL;
	char* pass = NULL;
	int n;
	int length;
	gpointer result_str = NULL;

	xmlDocPtr doc = NULL;
	xmlNodeSetPtr nodes = NULL;
	xmlXPathContextPtr ctx = NULL;
	xmlXPathObjectPtr path = NULL;

	GtkTextIter iter;

	PIXBUF_CACHE* pixbuf_cache = NULL;

	/* making basic auth info */
	gdk_threads_enter();
	mail = (char*)g_object_get_data(G_OBJECT(window), "mail");
	pass = (char*)g_object_get_data(G_OBJECT(window), "pass");
	gdk_threads_leave();

	memset(url, 0, sizeof(url));
	user_id = g_object_get_data(G_OBJECT(window), "user_id");
	user_name = g_object_get_data(G_OBJECT(window), "user_name");
	status_id = g_object_get_data(G_OBJECT(window), "status_id");
	if (status_id) {
		snprintf(url, sizeof(url)-1, SERVICE_THREAD_STATUS_URL, status_id);
		/* status_id is temporary value */
		g_free(status_id);
		g_object_set_data(G_OBJECT(window), "status_id", NULL);
	}
	else
	if (user_id)
		snprintf(url, sizeof(url)-1, SERVICE_FRIENDS_STATUS_URL, user_id);
	else
		strncpy(url, SERVICE_SELF_STATUS_URL, sizeof(url)-1);
	memset(auth, 0, sizeof(auth));
	snprintf(auth, sizeof(auth)-1, "%s:%s", mail, pass);

	/* initialize callback data */
	initialize_http_response();

	/* perform http */
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handle_returned_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, handle_returned_header);
	if (last_condition[0] != 0) {
		headers = curl_slist_append(headers, last_condition);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	}
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, APP_NAME);
	res = curl_easy_perform(curl);
	res == CURLE_OK ? curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &status) : res;
	curl_easy_cleanup(curl);
	if (headers) curl_slist_free_all(headers);

	if (status == 0) {
		result_str = g_strdup(_("no server response"));
		goto leave;
	}
	recv_data = malloc(response_size+1);
	memset(recv_data, 0, response_size+1);
	memcpy(recv_data, response_data, response_size);
	if (status == 304)
		goto leave;
	if (response_mime && strcmp(response_mime, "application/xml")) {
		result_str = g_strdup(_("unknown server response"));
		goto leave;
	}
	if (status != 200) {
		/* failed to get xml */
		if (response_data) {
			char* message = xml_decode_alloc(recv_data);
			result_str = g_strdup(message);
			free(message);
		} else
			result_str = g_strdup(_("unknown server response"));
		if (status == 401) {
			if (mail) free(mail);
			if (pass) free(pass);
			g_object_set_data(G_OBJECT(window), "mail", NULL);
			g_object_set_data(G_OBJECT(window), "pass", NULL);
		}
		goto leave;
	}
	if (response_cond) {
		if (!strncmp(response_cond, "ETag: ", 6))
			sprintf(last_condition, "If-None-Match: %s", response_cond+6);
		else
		if (!strncmp(response_cond, "Last-Modified: ", 15))
			sprintf(last_condition, "If-Modified-Since: %s", response_cond+15);
	}

	/* parse xml */
	doc = xmlParseDoc((xmlChar*)recv_data);
	if (!doc) {
		if (recv_data)
			result_str = g_strdup(recv_data);
		else
			result_str = g_strdup(_("unknown server response"));
		goto leave;
	}

	/* create xpath query */
	ctx = xmlXPathNewContext(doc);
	if (!ctx) {
		result_str = g_strdup(_("unknown server response"));
		goto leave;
	}
	path = xmlXPathEvalExpression((xmlChar*)"/statuses/status", ctx);
	if (!path) {
		result_str = g_strdup(_("unknown server response"));
		goto leave;
	}
	nodes = path->nodesetval;

	if (user_name)
		title = g_strdup_printf("%s - %s", APP_TITLE, user_name);
	else
	if (user_id)
		title = g_strdup_printf("%s - (%s)", APP_TITLE, user_id);
	else
		title = g_strdup(APP_TITLE);
	gtk_window_set_title(GTK_WINDOW(window), title);
	g_free(title);

	gdk_threads_enter();
	buffer = (GtkTextBuffer*)g_object_get_data(G_OBJECT(window), "buffer");
	date_tag = (GtkTextTag*)g_object_get_data(G_OBJECT(buffer), "date_tag");
	gtk_text_buffer_set_text(buffer, "", 0);
	gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
	gdk_threads_leave();

	/* allocate pixbuf cache buffer */
	length = xmlXPathNodeSetGetLength(nodes);
	pixbuf_cache = malloc(length*sizeof(PIXBUF_CACHE));
	memset(pixbuf_cache, 0, length*sizeof(PIXBUF_CACHE));

	/* make the friends timelines */
	for(n = 0; n < length; n++) {
		char* id = NULL;
		char* icon = NULL;
		char* real = NULL;
		char* name = NULL;
		char* text = NULL;
		char* desc = NULL;
		char* date = NULL;
		GdkPixbuf* pixbuf = NULL;
		int cache;

		/* status nodes */
		xmlNodePtr status = nodes->nodeTab[n];
		if (status->type != XML_ATTRIBUTE_NODE && status->type != XML_ELEMENT_NODE && status->type != XML_CDATA_SECTION_NODE) continue;
		status = status->children;
		while(status) {
			if (!strcmp("created_at", (char*)status->name)) date = (char*)status->children->content;
			if (!strcmp("text", (char*)status->name)) {
				if (status->children) text = (char*)status->children->content;
			}
			/* user nodes */
			if (!strcmp("user", (char*)status->name)) {
				xmlNodePtr user = status->children;
				while(user) {
					if (!strcmp("id", (char*)user->name)) id = XML_CONTENT(user);
					if (!strcmp("name", (char*)user->name)) real = XML_CONTENT(user);
					if (!strcmp("screen_name", (char*)user->name)) name = XML_CONTENT(user);
					if (!strcmp("profile_image_url", (char*)user->name)) {
						icon = XML_CONTENT(user);
						icon = (char*)g_strchomp((gchar*)icon);
						icon = (char*)g_strchug((gchar*)icon);
					}
					if (!strcmp("description", (char*)user->name)) desc = XML_CONTENT(user);
					user = user->next;
				}
			}
			status = status->next;
		}

		/**
		 * avoid to duplicate downloading of icon.
		 */
		for(cache = 0; cache < length; cache++) {
			if (!pixbuf_cache[cache].id) break;
			if (!strcmp(pixbuf_cache[cache].id, id)) {
				pixbuf = pixbuf_cache[cache].pixbuf;
				break;
			}
		}
		if (!pixbuf) {
			pixbuf = url2pixbuf((char*)icon, NULL);
			if (pixbuf) {
				pixbuf_cache[cache].id = id;
				pixbuf_cache[cache].pixbuf = pixbuf;
			}
		}

		/**
		 * layout:
		 *
		 * [icon] [name:name_tag]
		 * [message]
		 * [date:date_tag]
		 *
		 */
		gdk_threads_enter();
		if (pixbuf) gtk_text_buffer_insert_pixbuf(buffer, &iter, pixbuf);
		gtk_text_buffer_insert(buffer, &iter, " ", -1);
		name_tag = gtk_text_buffer_create_tag(
				buffer,
				NULL,
				"scale",
				PANGO_SCALE_LARGE,
				"underline",
				PANGO_UNDERLINE_SINGLE,
				"weight",
				PANGO_WEIGHT_BOLD,
				"foreground",
				"#0000FF",
				NULL);
		g_object_set_data(G_OBJECT(name_tag), "user_id", g_strdup(id));
		g_object_set_data(G_OBJECT(name_tag), "user_name", g_strdup(name));
		g_object_set_data(G_OBJECT(name_tag), "user_description", g_strdup(desc));
		gtk_text_buffer_insert_with_tags(buffer, &iter, name, -1, name_tag, NULL);
		gtk_text_buffer_insert(buffer, &iter, "\n", -1);
		text = xml_decode_alloc(text);
		insert_status_text(buffer, &iter, text);
		gtk_text_buffer_insert(buffer, &iter, "\n", -1);
		gtk_text_buffer_insert_with_tags(buffer, &iter, date, -1, date_tag, NULL);
		free(text);
		gtk_text_buffer_insert(buffer, &iter, "\n\n", -1);
		gdk_threads_leave();
	}
	free(pixbuf_cache);

	gdk_threads_enter();
	gtk_text_buffer_set_modified(buffer, FALSE) ;
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_buffer_place_cursor(buffer, &iter);
	gdk_threads_leave();

leave:
	if (recv_data) free(recv_data);
	if (path) xmlXPathFreeObject(path);
	if (ctx) xmlXPathFreeContext(ctx);
	if (doc) xmlFreeDoc(doc);

	/* cleanup callback data */
	terminate_http_response();

	return result_str;
}

static void update_friends_statuses(GtkWidget* widget, gpointer user_data) {
	gpointer result;
	GtkWidget* window = (GtkWidget*)user_data;
	GtkWidget* toolbox = (GtkWidget*)g_object_get_data(G_OBJECT(window), "toolbox");
	char* mail = (char*)g_object_get_data(G_OBJECT(window), "mail");
	char* pass = (char*)g_object_get_data(G_OBJECT(window), "pass");

	if (!mail || !pass) {
		if (!login_dialog(window)) return;
	}

	is_processing = TRUE;

	stop_reload_timer(window);
	gtk_widget_set_sensitive(toolbox, FALSE);
	result = process_func(update_friends_statuses_thread, window, window, _("updating statuses..."));
	if (result) {
		/* show error message */
		error_dialog(window, result);
		g_free(result);
	}
	gtk_widget_set_sensitive(toolbox, TRUE);
	start_reload_timer(window);

	is_processing = FALSE;
}

static void update_self_status(GtkWidget* widget, gpointer user_data) {
	GtkWidget* window = (GtkWidget*)user_data;
	GtkWidget* toolbox = (GtkWidget*)g_object_get_data(G_OBJECT(window), "toolbox");
	gchar* old_data;

	old_data = g_object_get_data(G_OBJECT(window), "user_id");
	if (old_data) g_free(old_data);
	g_object_set_data(G_OBJECT(window), "user_id", NULL);
	old_data = g_object_get_data(G_OBJECT(window), "user_name");
	if (old_data) g_free(old_data);
	g_object_set_data(G_OBJECT(window), "user_name", NULL);

	update_friends_statuses(NULL, window);
}

/**
 * post my status
 */
static gpointer post_status_thread(gpointer data) {
	GtkWidget* window = (GtkWidget*)data;
	GtkWidget* entry = NULL;
	CURL* curl = NULL;
	CURLcode res = CURLE_OK;
	struct curl_slist *headers = NULL;
	int status = 0;

	char url[2048];
	char auth[512];
	char* message = NULL;
	char* sanitized_message = NULL;
	char* mail = NULL;
	char* pass = NULL;
	gpointer result_str = NULL;

	gdk_threads_enter();
	mail = (char*)g_object_get_data(G_OBJECT(window), "mail");
	pass = (char*)g_object_get_data(G_OBJECT(window), "pass");
	entry = (GtkWidget*)g_object_get_data(G_OBJECT(window), "entry");
	message = (char*)gtk_entry_get_text(GTK_ENTRY(entry));
	gdk_threads_leave();
	if (!message || strlen(message) == 0) return NULL;

	/* making authenticate info */
	memset(url, 0, sizeof(url));
	strncpy(url, SERVICE_UPDATE_URL, sizeof(url)-1);
	sanitized_message = sanitize_message_alloc(message);
	if (!message) return NULL;
	message = sanitized_message;
	message = url_encode_alloc(message);
	free(sanitized_message);
	if (message) {
		strncat(url, "?status=", sizeof(url)-1);;
		strncat(url, message, sizeof(url)-1);
		free(message);
	}
	memset(auth, 0, sizeof(auth));
	snprintf(auth, sizeof(auth)-1, "%s:%s", mail, pass);

	/* initialize callback data */
	initialize_http_response();

	headers = curl_slist_append(headers, "X-Twitter-Client: "APP_NAME);
	headers = curl_slist_append(headers, "X-Twitter-Client-Version: "APP_VERSION);
	headers = curl_slist_append(headers, "X-Twitter-Client-URL: "APP_URL);

	/* perform http */
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, handle_returned_data);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, handle_returned_header);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, APP_NAME);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	res = curl_easy_perform(curl);
	res == CURLE_OK ? curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &status) : res;
	curl_easy_cleanup(curl);
	if (headers) curl_slist_free_all(headers);

	if (status != 200) {
		/* failed to the post */
		if (response_data) {
			char* message;
			char* recv_data = malloc(response_size+1);
			memset(recv_data, 0, response_size+1);
			memcpy(recv_data, response_data, response_size);
			message = xml_decode_alloc(recv_data);
			result_str = g_strdup(message);
			free(message);
		} else
			result_str = g_strdup(_("unknown server response"));
		goto leave;
	} else {
		/* succeeded to the post */
		gdk_threads_enter();
		gtk_entry_set_text(GTK_ENTRY(entry), "");
		gdk_threads_leave();
	}

leave:
	/* cleanup callback data */
	if (response_cond) free(response_cond);
	if (response_mime) free(response_mime);
	if (response_data) free(response_data);
	response_data = NULL;
	response_mime = NULL;
	response_cond = NULL;
	response_size = 0;
	return result_str;
}

static void post_status(GtkWidget* widget, gpointer user_data) {
	gpointer result;
	GtkWidget* window = (GtkWidget*)user_data;
	GtkWidget* toolbox = (GtkWidget*)g_object_get_data(G_OBJECT(window), "toolbox");
	char* mail = (char*)g_object_get_data(G_OBJECT(window), "mail");
	char* pass = (char*)g_object_get_data(G_OBJECT(window), "pass");

	if (!mail || !pass) {
		if (!login_dialog(window)) return;
	}

	is_processing = TRUE;

	gtk_widget_set_sensitive(toolbox, FALSE);
	result = process_func(post_status_thread, window, window, _("posting status..."));
	if (!result) {
		last_condition[0] = 0;
		result = process_func(update_friends_statuses_thread, window, window, _("updating statuses..."));
	}
	if (result) {
		/* show error message */
		error_dialog(window, result);
		g_free(result);
	}
	gtk_widget_set_sensitive(toolbox, TRUE);

	is_processing = FALSE;
}

/**
 * enter key handler
 */
static gboolean on_entry_keyp_ress(GtkWidget *widget, GdkEventKey* event, gpointer user_data) {
	char* message = (char*)gtk_entry_get_text(GTK_ENTRY(widget));

	if (is_processing) return FALSE;

	if (!message || strlen(message) == 0) return FALSE;
	if (event->keyval == GDK_Return)
		post_status(widget, user_data);
	reset_reload_timer(gtk_widget_get_toplevel(widget));
	return FALSE;
}

/**
 * login dialog func
 */
static gboolean login_dialog(GtkWidget* window) {
	GtkWidget* dialog = NULL;
	GtkWidget* table = NULL;
	GtkWidget* label = NULL;
	GtkWidget* mail = NULL;
	GtkWidget* pass = NULL;
	gboolean ret = FALSE;

	/* login dialog */
	dialog = gtk_dialog_new();
	gtk_dialog_add_buttons(GTK_DIALOG(dialog),
			GTK_STOCK_OK, GTK_RESPONSE_OK,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	gtk_window_set_title(GTK_WINDOW(dialog), _(APP_TITLE" Login"));

	/* layout table */
	table = gtk_table_new(2, 2, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(table), 6);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table); 

	/* mail */
	label = gtk_label_new(_("_Mail:"));
	gtk_label_set_use_underline(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
	gtk_table_attach(
			GTK_TABLE(table),
			label,
			0, 1,                   4, 5,
			GTK_FILL,               GTK_FILL,
			0,                      0);
	mail = gtk_entry_new();
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), mail);
	gtk_table_attach(
			GTK_TABLE(table),
			mail,
			1, 2,                   4, 5,
			(GtkAttachOptions)(GTK_EXPAND|GTK_FILL), GTK_FILL,
			0,                      0);

	/* pass */
	label = gtk_label_new(_("_Password:"));
	gtk_label_set_use_underline(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
	gtk_table_attach(
			GTK_TABLE(table),
			label,
			0, 1,                   5, 6,
			GTK_FILL,               GTK_FILL,
			0,                      0);
	pass = gtk_entry_new();
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), pass);
	gtk_entry_set_visibility(GTK_ENTRY(pass), FALSE);
	gtk_table_attach(
			GTK_TABLE(table),
			pass,
			1, 2,                   5, 6,
			(GtkAttachOptions)(GTK_EXPAND|GTK_FILL), GTK_FILL,
			0,                      0);

	/* show modal dialog */
	gtk_widget_show_all(table);
	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_transient_for(
			GTK_WINDOW(dialog),
			GTK_WINDOW(window));
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_widget_show_all(dialog);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		/* set mail/pass value to window object */
		char* mail_text = (char*)gtk_entry_get_text(GTK_ENTRY(mail));
		char* pass_text = (char*)gtk_entry_get_text(GTK_ENTRY(pass));
		g_object_set_data(G_OBJECT(window), "mail", strdup(mail_text));
		g_object_set_data(G_OBJECT(window), "pass", strdup(pass_text));
		save_config(window);
		ret = TRUE;
	}

	gtk_widget_destroy(dialog);
	return ret;
}

static void textview_change_cursor(GtkWidget* textview, gint x, gint y) {
	static gboolean hovering_over_link = FALSE;
	GSList *tags = NULL;
	GtkWidget* toplevel;
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkTooltips* tooltips = NULL;
	gboolean hovering = FALSE;
	int len, n;

	if (is_processing) return;

	toplevel = gtk_widget_get_toplevel(textview);
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(textview), &iter, x, y);
	tooltips = (GtkTooltips*)g_object_get_data(G_OBJECT(toplevel), "tooltips");

	tags = gtk_text_iter_get_tags(&iter);
	if (tags) {
		len = g_slist_length(tags);
		for(n = 0; n < len; n++) {
			GtkTextTag* tag = (GtkTextTag*)g_slist_nth_data(tags, n);
			if (tag) {
				gpointer url;
				url = g_object_get_data(G_OBJECT(tag), "url");
				if (url) {
					hovering = TRUE;
					break;
				}
				url = g_object_get_data(G_OBJECT(tag), "user_id");
				if (url) {
					hovering = TRUE;
					break;
				}
			}
		}
		g_slist_free(tags);
	}
	if (hovering != hovering_over_link) {
		char* message = NULL;
		hovering_over_link = hovering;
		gdk_window_set_cursor(
				gtk_text_view_get_window(
					GTK_TEXT_VIEW(textview),
					GTK_TEXT_WINDOW_TEXT),
				hovering_over_link ? hand_cursor : regular_cursor);
		/* TODO: tooltips for user icon.
		if (hovering_over_link)
			message = _("what are you doing?"),
		gtk_tooltips_set_tip(
				GTK_TOOLTIPS(tooltips),
				textview,
				message, message);
		*/
	}
}

static gboolean textview_event_after(GtkWidget* textview, GdkEvent* ev) {
	GtkWidget* toplevel;
	GtkTextIter start, end, iter;
	GtkTextBuffer *buffer;
	GdkEventButton *event;
	GSList *tags = NULL;
	gint x, y;
	int len, n;
	gchar* url = NULL;
	gchar* user_id = NULL;
	gchar* user_name = NULL;

	if (is_processing) return FALSE;

	if (ev->type != GDK_BUTTON_RELEASE) return FALSE;
	event = (GdkEventButton*)ev;
	if (event->button != 1) return FALSE;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	gtk_text_buffer_get_selection_bounds(buffer, &start, &end);
	if (gtk_text_iter_get_offset(&start) != gtk_text_iter_get_offset(&end)) {
		return FALSE;
	}
	gtk_text_view_window_to_buffer_coords(
			GTK_TEXT_VIEW(textview), 
			GTK_TEXT_WINDOW_WIDGET,
			(gint)event->x, (gint)event->y, &x, &y);
	gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(textview), &iter, x, y);

	tags = gtk_text_iter_get_tags(&iter);
	if (tags) {
		len = g_slist_length(tags);
		for(n = 0; n < len; n++) {
			GtkTextTag* tag = (GtkTextTag*)g_slist_nth_data(tags, n);
			if (tag) {
				gpointer tag_data;
				tag_data = g_object_get_data(G_OBJECT(tag), "url");
				if (tag_data) {
					url = tag_data;
					break;
				}

				user_id = g_object_get_data(G_OBJECT(tag), "user_id");
				user_name = g_object_get_data(G_OBJECT(tag), "user_name");
				if (user_id || user_name) {
					url = g_strdup_printf("@%s", user_name);
					break;
				}
			}
		}
		g_slist_free(tags);
	}

	if (!url) return FALSE;

	toplevel = gtk_widget_get_toplevel(textview);
	if (*url == '@') {
		if (!is_processing) {
			gchar* old_data;
			old_data = g_object_get_data(G_OBJECT(toplevel), "user_id");
			if (old_data) g_free(old_data);
			old_data = g_object_get_data(G_OBJECT(toplevel), "user_name");
			if (old_data) g_free(old_data);

			g_object_set_data(G_OBJECT(toplevel), "user_id", g_strdup(user_id));
			g_object_set_data(G_OBJECT(toplevel), "user_name", g_strdup(user_name));
			update_friends_statuses(NULL, toplevel);
		}
	} else
	if (!strncmp(url, ">>", 2)) {
		if (!is_processing) {
			gchar* status_id = url+2;
			g_object_set_data(G_OBJECT(toplevel), "status_id", g_strdup(status_id));
			update_friends_statuses(NULL, toplevel);
		}
	} else {
#ifdef _WIN32
		ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOW);
#else
		gchar* command = g_strdup_printf("firefox \"%s\"", url);
		g_spawn_command_line_async(command, NULL);
		g_free(command);
#endif
		gtk_widget_queue_draw(toplevel);
	}
	g_free(url);
	return FALSE;
}

static gboolean textview_motion(GtkWidget* textview, GdkEventMotion* event) {
	gint x, y;
	x = y = 0;
	gtk_text_view_window_to_buffer_coords(
			GTK_TEXT_VIEW(textview),
			GTK_TEXT_WINDOW_WIDGET,
			(gint)event->x, (gint)event->y, &x, &y);
	textview_change_cursor(textview, x, y);
	gdk_window_get_pointer(textview->window, NULL, NULL, NULL);
	return FALSE;
}

static gboolean textview_visibility(GtkWidget* textview, GdkEventVisibility* event) {
	gint wx, wy, x, y;
	wx = wy = x = y = 0;
	gdk_window_get_pointer(textview->window, &wx, &wy, NULL);
	gtk_text_view_window_to_buffer_coords(
			GTK_TEXT_VIEW(textview),
			GTK_TEXT_WINDOW_WIDGET,
			wx, wy, &x, &y);
	textview_change_cursor(textview, x, y);
	gdk_window_get_pointer(textview->window, NULL, NULL, NULL);
	return FALSE;
}

static void buffer_delete_range(GtkTextBuffer* buffer, GtkTextIter* start, GtkTextIter* end, gpointer user_data) {
	GtkTextIter* iter = gtk_text_iter_copy(end);
	while(iter) {
		GSList* tags = NULL;
		GtkTextTag* tag;
		int len, n;
		if (!gtk_text_iter_backward_char(iter)) break;
		if (!gtk_text_iter_in_range(iter, start, end)) break;
		tags = gtk_text_iter_get_tags(iter);
		if (!tags) continue;
		len = g_slist_length(tags);
		for(n = 0; n < len; n++) {
			gpointer tag_data;

			tag = (GtkTextTag*)g_slist_nth_data(tags, n);
			if (!tag) continue;

			tag_data = g_object_get_data(G_OBJECT(tag), "url");
			if (tag_data) g_free(tag_data);
			g_object_set_data(G_OBJECT(tag), "url", NULL);

			tag_data = g_object_get_data(G_OBJECT(tag), "user_id");
			if (tag_data) g_free(tag_data);
			g_object_set_data(G_OBJECT(tag), "user_id", NULL);

			tag_data = g_object_get_data(G_OBJECT(tag), "user_name");
			if (tag_data) g_free(tag_data);
			g_object_set_data(G_OBJECT(tag), "user_name", NULL);

			tag_data = g_object_get_data(G_OBJECT(tag), "user_description");
			if (tag_data) g_free(tag_data);
			g_object_set_data(G_OBJECT(tag), "user_description", NULL);
		}
		g_slist_free(tags);
	}
	gtk_text_iter_free(iter);
}

/**
 * timer register
 */
static guint reload_timer(gpointer data) {
	GtkWidget* window = (GtkWidget*)data;
	gdk_threads_enter();
	update_friends_statuses(NULL, window);
	gdk_threads_leave();
	return 0;
}

static void stop_reload_timer(GtkWidget* toplevel) {
	if (timer_tag != 0) g_source_remove(timer_tag);
}

static void start_reload_timer(GtkWidget* toplevel) {
	stop_reload_timer(toplevel);
	timer_tag = g_timeout_add(RELOAD_TIMER_SPAN, (GSourceFunc)reload_timer, toplevel);
}

static void reset_reload_timer(GtkWidget* toplevel) {
	stop_reload_timer(toplevel);
	start_reload_timer(toplevel);
}

/**
 * configuration
 */
static int load_config(GtkWidget* window) {
	char* mail = NULL;
	char* pass = NULL;
	const gchar* confdir = g_get_user_config_dir();
	gchar* conffile = g_build_filename(confdir, APP_NAME, "config", NULL);
	char buf[BUFSIZ];
	FILE *fp = fopen(conffile, "r");
	g_free(conffile);
	if (!fp) return -1;
	while(fgets(buf, sizeof(buf), fp)) {
		gchar* line = g_strchomp(buf);
		if (!strncmp(line, "mail=", 5))
			g_object_set_data(G_OBJECT(window), "mail", g_strdup(line+5));
		if (!strncmp(line, "pass=", 5))
			g_object_set_data(G_OBJECT(window), "pass", g_strdup(line+5));
	}
	fclose(fp);
	return 0;
}

static int save_config(GtkWidget* window) {
	char* mail = (char*)g_object_get_data(G_OBJECT(window), "mail");
	char* pass = (char*)g_object_get_data(G_OBJECT(window), "pass");
	gchar* confdir = (gchar*)g_get_user_config_dir();
	gchar* conffile = NULL;
	FILE* fp = NULL;

	confdir = g_build_path(G_DIR_SEPARATOR_S, confdir, APP_NAME, NULL);
	g_mkdir_with_parents(confdir, 0700);
	conffile = g_build_filename(confdir, "config", NULL);
	g_free(confdir);
	fp = fopen(conffile, "w");
	g_free(conffile);
	if (!fp) return -1;
	fprintf(fp, "mail=%s\n", mail ? mail : "");
	fprintf(fp, "pass=%s\n", pass ? pass : "");
	fclose(fp);
	return 0;
}

/**
 * main entry
 */
int main(int argc, char* argv[]) {
	/* widgets */
	GtkWidget* window = NULL;
	GtkWidget* vbox = NULL;
	GtkWidget* hbox = NULL;
	GtkWidget* toolbox = NULL;
	GtkWidget* swin = NULL;
	GtkWidget* textview = NULL;
	GtkWidget* image = NULL;
	GtkWidget* button = NULL;
	GtkWidget* entry = NULL;
	PangoFontDescription* pangoFont = NULL;
	GtkTooltips* tooltips = NULL;

	GtkTextBuffer* buffer = NULL;
	GtkTextTag* date_tag = NULL;

#ifdef _LIBINTL_H
	setlocale(LC_CTYPE, "");

#ifdef LOCALE_SISO639LANGNAME
	if (getenv("LANG") == NULL) {
		char szLang[256] = {0};
		if (GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, szLang, sizeof(szLang))) {
			char szEnv[256] = {0};
			sprintf(szEnv, "LANG=%s", szLang);
			putenv(szEnv);
		}
	}
#endif

	bindtextdomain(APP_NAME, LOCALE_DIR);
	bind_textdomain_codeset(APP_NAME, "utf-8");
	textdomain(APP_NAME);
#endif

	g_thread_init(NULL);
	gdk_threads_init();
	gdk_threads_enter();

	gtk_init(&argc, &argv);

	/* main window */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), APP_TITLE);
	g_signal_connect(G_OBJECT(window), "delete-event", gtk_main_quit, window);

	/* link cursor */
	hand_cursor = gdk_cursor_new(GDK_HAND2);
	regular_cursor = gdk_cursor_new(GDK_XTERM);

	/* tooltips */
	tooltips = gtk_tooltips_new();
	g_object_set_data(G_OBJECT(window), "tooltips", tooltips);

	/* virtical container box */
	vbox = gtk_vbox_new(FALSE, 6);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	/* title logo */
	image = gtk_image_new_from_pixbuf(gdk_pixbuf_new_from_file(DATA_DIR"/"SERVICE_NAME".png", NULL));
	gtk_box_pack_start(GTK_BOX(vbox), image, FALSE, TRUE, 0);

	/* status viewer on scrolled window */
	textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_CHAR);
	g_signal_connect(textview, "motion-notify-event", G_CALLBACK(textview_motion), NULL);
	g_signal_connect(textview, "visibility-notify-event", G_CALLBACK(textview_visibility), NULL);
	g_signal_connect(textview, "event-after", G_CALLBACK(textview_event_after), NULL);

	swin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(
			GTK_SCROLLED_WINDOW(swin),
			GTK_POLICY_NEVER, 
			GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(swin), textview);
	gtk_container_add(GTK_CONTAINER(vbox), swin);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	g_signal_connect(G_OBJECT(buffer), "delete-range", G_CALLBACK(buffer_delete_range), NULL);
	g_object_set_data(G_OBJECT(window), "buffer", buffer);

	/* tags for string attributes */
	date_tag = gtk_text_buffer_create_tag(
			buffer,
			"date_tag",
			"scale",
			PANGO_SCALE_X_SMALL,
			"style",
			PANGO_STYLE_ITALIC,
			"foreground",
			"#005500",
			NULL);
	g_object_set_data(G_OBJECT(buffer), "date_tag", date_tag);

	/* toolbox */
	toolbox = gtk_vbox_new(FALSE, 6);
	gtk_box_pack_start(GTK_BOX(vbox), toolbox, FALSE, TRUE, 0);
	g_object_set_data(G_OBJECT(window), "toolbox", toolbox);

	/* horizontal container box for buttons and entry */
	hbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start(GTK_BOX(toolbox), hbox, FALSE, TRUE, 0);

	/* home button */
	button = gtk_button_new();
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(update_self_status), window);
	image = gtk_image_new_from_pixbuf(gdk_pixbuf_new_from_file(DATA_DIR"/home.png", NULL));
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	gtk_tooltips_set_tip(
			GTK_TOOLTIPS(tooltips),
			button,
			_("go home"),
			_("go home"));

	/* update button */
	button = gtk_button_new();
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(update_friends_statuses), window);
	image = gtk_image_new_from_pixbuf(gdk_pixbuf_new_from_file(DATA_DIR"/reload.png", NULL));
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	gtk_tooltips_set_tip(
			GTK_TOOLTIPS(tooltips),
			button,
			_("reload statuses"),
			_("reload statuses"));

	/* post button */
	button = gtk_button_new();
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(post_status), window);
	image = gtk_image_new_from_pixbuf(gdk_pixbuf_new_from_file(DATA_DIR"/post.png", NULL));
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	gtk_tooltips_set_tip(
			GTK_TOOLTIPS(tooltips),
			button,
			_("post status"),
			_("post status"));

	/* text entry */
	entry = gtk_entry_new();
	g_object_set_data(G_OBJECT(window), "entry", entry);
	g_signal_connect(G_OBJECT(entry), "key-press-event", G_CALLBACK(on_entry_keyp_ress), window);
	gtk_box_pack_start(GTK_BOX(toolbox), entry, FALSE, TRUE, 0);
	//gtk_widget_set_size_request(entry, -1, 50);
	gtk_tooltips_set_tip(
			GTK_TOOLTIPS(tooltips),
			entry,
			_("what are you doing?"),
			_("what are you doing?"));

	/* request initial window size */
	gtk_widget_set_size_request(window, 300, 400);
	gtk_widget_show_all(vbox);
	gtk_widget_show(window);

	load_config(window);

	/*
	pangoFont = pango_font_description_new();
	pango_font_description_set_family(pangoFont, "meiryo");
	gtk_widget_modify_font(textview, pangoFont);
	pango_font_description_free(pangoFont);
	*/


	update_friends_statuses(window, window);
	gtk_main();

	gdk_threads_leave();

	return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hCurInst, HINSTANCE hPrevInst, LPSTR lpsCmdLine, int nCmdShow)
{
	return main(__argc, __argv);
}
#endif
