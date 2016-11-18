
#ifndef UTILS_EC2_META_H
#define UTILS_EC2_META_H 1

#include <curl/curl.h>

void ec2_meta_add_headers(struct curl_slist **headers);

int ec2_meta_init();

#endif
