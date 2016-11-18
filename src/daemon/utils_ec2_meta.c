
#include "collectd.h"

#include "plugin.h"
#include "common.h"
#include "utils_format_json.h"
#include "utils_format_kairosdb.h"

#include <curl/curl.h>

#define CURL_TIMEOUT_MS	500	/*ms*/
#define EC2_FIELD_SIZE	512

#define BASE_EC2_URL	"http://169.254.169.254/latest/meta-data/"

struct ec2_meta_field {
	char	buf[EC2_FIELD_SIZE];
	size_t	filled;
};

struct ec2_meta {
	struct ec2_meta_field *instance_id;
	struct ec2_meta_field *instance_type;
	struct ec2_meta_field *az;
};


static size_t ec2_meta_callback(void *buf, size_t size,
    size_t nmemb, void *user_data)
{
	struct ec2_meta_field *field = (struct ec2_meta_field *)user_data;

	if (field == NULL) {
		return 0;
	}

	size_t len = size * nmemb;

	if (field->filled + len >= sizeof(field->buf)) {
		return (0);
	}

	memcpy(field->buf + field->filled, (char *)buf, len);
	field->filled += len;
	field->buf[field->filled] = '\0';

	return len;
}

static struct ec2_meta_field *ec2_meta_get_field(const char *path)
{
	char url[1024];
	char errbuf[CURL_ERROR_SIZE];

	snprintf(url, sizeof(url), "%s%s", BASE_EC2_URL, path);

	CURL *curl;

	struct ec2_meta_field *field = calloc(1, sizeof(*field));
	if (field == NULL) {
		ERROR("ec2_meta: failed to allocate field");
		return NULL;
	}

	curl = curl_easy_init();
	if (curl == NULL) {
		ERROR ("ec2_meta: curl_easy_init failed");
		free(field);
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ec2_meta_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, field);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);

	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, CURL_TIMEOUT_MS);

	int status = curl_easy_perform(curl);
	if (status != CURLE_OK) {
		ERROR("ec2_meta: curl_easy_perform failed with status %d: %s",
		    status, errbuf);
		curl_easy_cleanup(curl);
		free(field);
		return NULL;
	}

	curl_easy_cleanup(curl);

	return field;
}

static struct ec2_meta *ec2_meta_get_fields()
{
	struct ec2_meta *ec2;

	ec2 = calloc(1, sizeof(*ec2));
	if (ec2 == NULL) {
		ERROR("ec2_meta: failed to allocate memory");
		return NULL;
	}

	ec2->instance_id = ec2_meta_get_field("instance_id");
	if (ec2->instance_id == NULL) {
		ERROR("ec2_meta: failed to get field instance_id");
		goto failed;
	}

	ec2->instance_type = ec2_meta_get_field("instance_type");
	if (ec2->instance_type == NULL) {
		ERROR("ec2_meta: failed to get field instance_type");
		goto failed;
	}

	ec2->az = ec2_meta_get_field("placement/availability-zone");
	if (ec2->az == NULL) {
		ERROR("ec2_meta: failed to get field availability-zone");
		goto failed;
	}

	return ec2;

  failed:

	if (ec2->az != NULL)
		free(ec2->az);
	if (ec2->instance_type != NULL)
		free(ec2->instance_type);
	if (ec2->instance_id != NULL)
		free(ec2->instance_id);

	free(ec2);
	return NULL;
}

int ec2_meta_init()
{
	curl_global_init(CURL_GLOBAL_SSL);

	struct ec2_meta *ec2 = ec2_meta_get_fields();

	if (ec2 != NULL) {
		fprintf(stderr,
		    "DETECTED AWS EC2:\n"
		    "\tinstance-id: %s\n"
		    "\tinstance-type: %s\n"
		    "\taz: %s\n",
		    ec2->instance_id->buf,
		    ec2->instance_type->buf,
		    ec2->az->buf);
	}

	return ec2 != NULL ? 0 : -1;
}
