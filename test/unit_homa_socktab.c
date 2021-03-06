/* Copyright (c) 2019, Stanford University
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "homa_impl.h"
#define KSELFTEST_NOT_MAIN 1
#include "kselftest_harness.h"
#include "ccutils.h"
#include "mock.h"
#include "utils.h"

#define n(x) htons(x)
#define N(x) htonl(x)

FIXTURE(homa_socktab) {
	struct homa homa;
	struct homa_sock hsk;
};
FIXTURE_SETUP(homa_socktab)
{
	homa_init(&self->homa);
	mock_sock_init(&self->hsk, &self->homa, 0, 0);
}
FIXTURE_TEARDOWN(homa_socktab)
{
	homa_destroy(&self->homa);
	unit_teardown();
}

TEST_F(homa_socktab, homa_port_hash)
{
	EXPECT_EQ(1023, homa_port_hash(0xffff));
	EXPECT_EQ(18, homa_port_hash(0x6012));
	EXPECT_EQ(99, homa_port_hash(99));
}

TEST_F(homa_socktab, homa_socktab_start_scan)
{
	struct homa_socktab_scan scan;
	homa_destroy(&self->homa);
	homa_init(&self->homa);
	mock_sock_init(&self->hsk, &self->homa, HOMA_MIN_CLIENT_PORT+100, 0);
	EXPECT_EQ(&self->hsk, homa_socktab_start_scan(&self->homa.port_map,
			&scan));
	EXPECT_EQ(100, scan.current_bucket);
}

TEST_F(homa_socktab, homa_socktab_next__basics)
{
	struct homa_sock hsk1, hsk2, hsk3, hsk4, *hsk;
	struct homa_socktab_scan scan;
	int first_port = 34000;
	homa_destroy(&self->homa);
	homa_init(&self->homa);
	mock_sock_init(&hsk1, &self->homa, first_port, 0);
	mock_sock_init(&hsk2, &self->homa, first_port+HOMA_SOCKTAB_BUCKETS, 0);
	mock_sock_init(&hsk3, &self->homa, first_port+2*HOMA_SOCKTAB_BUCKETS, 10);
	mock_sock_init(&hsk4, &self->homa, first_port+5, 20);
	hsk = homa_socktab_start_scan(&self->homa.port_map, &scan);
	EXPECT_EQ(first_port+2*HOMA_SOCKTAB_BUCKETS, hsk->client_port);
	hsk = homa_socktab_next(&scan);
	EXPECT_EQ(first_port+HOMA_SOCKTAB_BUCKETS, hsk->client_port);
	hsk = homa_socktab_next(&scan);
	EXPECT_EQ(first_port, hsk->client_port);
	hsk = homa_socktab_next(&scan);
	EXPECT_EQ(first_port+5, hsk->client_port);
	hsk = homa_socktab_next(&scan);
	EXPECT_EQ(NULL, hsk);
}
TEST_F(homa_socktab, homa_socktab_next__deleted_socket)
{
	struct homa_sock hsk1, hsk2, hsk3, *hsk;
	struct homa_socktab_scan scan;
	int first_port = 34000;
	homa_destroy(&self->homa);
	homa_init(&self->homa);
	mock_sock_init(&hsk1, &self->homa, first_port, 0);
	mock_sock_init(&hsk2, &self->homa, first_port+HOMA_SOCKTAB_BUCKETS, 0);
	mock_sock_init(&hsk3, &self->homa, first_port+2*HOMA_SOCKTAB_BUCKETS, 10);
	hsk = homa_socktab_start_scan(&self->homa.port_map, &scan);
	EXPECT_EQ(first_port+2*HOMA_SOCKTAB_BUCKETS, hsk->client_port);
	homa_sock_destroy(&hsk2);
	hsk = homa_socktab_next(&scan);
	EXPECT_EQ(first_port+HOMA_SOCKTAB_BUCKETS, hsk->client_port);
	EXPECT_EQ(NULL, hsk->homa);
	hsk = homa_socktab_next(&scan);
	EXPECT_EQ(first_port, hsk->client_port);
	hsk = homa_socktab_next(&scan);
	EXPECT_EQ(NULL, hsk);
}

TEST_F(homa_socktab, homa_sock_init__skip_port_in_use)
{
	struct homa_sock hsk2, hsk3;
	self->homa.next_client_port = 0xffff;
	mock_sock_init(&hsk2, &self->homa, 0, 0);
	mock_sock_init(&hsk3, &self->homa, 0, 0);
	EXPECT_EQ(65535, hsk2.client_port);
	EXPECT_EQ(32769, hsk3.client_port);
	homa_sock_destroy(&hsk2);
	homa_sock_destroy(&hsk3);
}

TEST_F(homa_socktab, homa_sock_shutdown)
{
	struct homa_interest interest1, interest2, interest3;
	EXPECT_FALSE(self->hsk.shutdown);
	list_add_tail(&interest1.request_links, &self->hsk.request_interests);
	list_add_tail(&interest2.request_links, &self->hsk.request_interests);
	list_add_tail(&interest3.response_links, &self->hsk.response_interests);
	homa_sock_shutdown(&self->hsk);
	EXPECT_TRUE(self->hsk.shutdown);
	EXPECT_STREQ("wake_up_process; wake_up_process; wake_up_process; "
		"wake_up_process", unit_log_get());
}

TEST_F(homa_socktab, homa_sock_destroy__basics)
{
	int client2, client3;
	struct homa_sock hsk2, hsk3;
	mock_sock_init(&hsk2, &self->homa, 0, 0);
	EXPECT_EQ(0, homa_sock_bind(&self->homa.port_map, &hsk2, 100));
	client2 = hsk2.client_port;
	mock_sock_init(&hsk3, &self->homa, 0, 0);
	client3 = hsk3.client_port;
	
	EXPECT_EQ(&hsk2, homa_sock_find(&self->homa.port_map, client2));
	EXPECT_EQ(&hsk2, homa_sock_find(&self->homa.port_map, 100));
	EXPECT_EQ(&hsk3, homa_sock_find(&self->homa.port_map, client3));
	
	homa_sock_destroy(&hsk2);
	
	EXPECT_EQ(NULL, homa_sock_find(&self->homa.port_map, client2));
	EXPECT_EQ(NULL, homa_sock_find(&self->homa.port_map, 100));
	EXPECT_EQ(&hsk3, homa_sock_find(&self->homa.port_map, client3));
	
	homa_sock_destroy(&hsk3);
	
	EXPECT_EQ(NULL, homa_sock_find(&self->homa.port_map, client2));
	EXPECT_EQ(NULL, homa_sock_find(&self->homa.port_map, 100));
	EXPECT_EQ(NULL, homa_sock_find(&self->homa.port_map, client3));
}
TEST_F(homa_socktab, homa_sock_destroy__wakeup_interests)
{
	struct homa_interest interest1, interest2, interest3;
	struct homa_sock hsk2;
	mock_sock_init(&hsk2, &self->homa, 0, 0);
	list_add_tail(&interest1.request_links, &hsk2.request_interests);
	list_add_tail(&interest2.request_links, &hsk2.request_interests);
	list_add_tail(&interest3.response_links, &hsk2.response_interests);
	homa_sock_destroy(&hsk2);
	EXPECT_STREQ("wake_up_process; wake_up_process; wake_up_process; "
		"wake_up_process", unit_log_get());
}

TEST_F(homa_socktab, homa_sock_bind)
{
	struct homa_sock hsk2;
	mock_sock_init(&hsk2, &self->homa, 0, 0);
	EXPECT_EQ(0, homa_sock_bind(&self->homa.port_map, &hsk2, 100));
	
	EXPECT_EQ(EINVAL, -homa_sock_bind(&self->homa.port_map, &self->hsk, 0));
	EXPECT_EQ(EINVAL, -homa_sock_bind(&self->homa.port_map, &self->hsk,
			HOMA_MIN_CLIENT_PORT + 100));
	
	EXPECT_EQ(EADDRINUSE, -homa_sock_bind(&self->homa.port_map, &self->hsk,
			100));
	EXPECT_EQ(0, -homa_sock_bind(&self->homa.port_map, &hsk2,
			100));
	
	EXPECT_EQ(0, -homa_sock_bind(&self->homa.port_map, &self->hsk,
			110));
	
	EXPECT_EQ(&self->hsk, homa_sock_find(&self->homa.port_map, 110));
	EXPECT_EQ(0, -homa_sock_bind(&self->homa.port_map, &self->hsk,
			120));
	EXPECT_EQ(NULL, homa_sock_find(&self->homa.port_map, 110));
	EXPECT_EQ(&self->hsk, homa_sock_find(&self->homa.port_map, 120));
	homa_sock_destroy(&hsk2);
}

TEST_F(homa_socktab, homa_sock_find__basics)
{
	struct homa_sock hsk2;
	mock_sock_init(&hsk2, &self->homa, 0, 0);
	EXPECT_EQ(0, homa_sock_bind(&self->homa.port_map, &hsk2, 100));
	EXPECT_EQ(&self->hsk, homa_sock_find(&self->homa.port_map,
			self->hsk.client_port));
	EXPECT_EQ(&hsk2, homa_sock_find(&self->homa.port_map,
			hsk2.client_port));
	EXPECT_EQ(&hsk2, homa_sock_find(&self->homa.port_map,
			hsk2.server_port));
	EXPECT_EQ(NULL, homa_sock_find(&self->homa.port_map,
			hsk2.server_port + 1));
	homa_sock_destroy(&hsk2);
}

TEST_F(homa_socktab, homa_sock_find__long_hash_chain)
{
	struct homa_sock hsk2, hsk3, hsk4;
	EXPECT_EQ(0, homa_sock_bind(&self->homa.port_map, &self->hsk, 13));
	mock_sock_init(&hsk2, &self->homa, 0, 0);
	EXPECT_EQ(0, homa_sock_bind(&self->homa.port_map, &hsk2,
			2*HOMA_SOCKTAB_BUCKETS + 13));
	mock_sock_init(&hsk3, &self->homa, 0, 0);
	EXPECT_EQ(0, homa_sock_bind(&self->homa.port_map, &hsk3,
			3*HOMA_SOCKTAB_BUCKETS + 13));
	mock_sock_init(&hsk4, &self->homa, 0, 0);
	EXPECT_EQ(0, homa_sock_bind(&self->homa.port_map, &hsk4,
			5*HOMA_SOCKTAB_BUCKETS + 13));
	
	EXPECT_EQ(&self->hsk, homa_sock_find(&self->homa.port_map,
			13));
	EXPECT_EQ(&hsk2, homa_sock_find(&self->homa.port_map,
			2*HOMA_SOCKTAB_BUCKETS + 13));
	EXPECT_EQ(&hsk3, homa_sock_find(&self->homa.port_map,
			3*HOMA_SOCKTAB_BUCKETS + 13));
	EXPECT_EQ(&hsk4, homa_sock_find(&self->homa.port_map,
			5*HOMA_SOCKTAB_BUCKETS + 13));
	
	homa_sock_destroy(&hsk2);
	homa_sock_destroy(&hsk3);
	homa_sock_destroy(&hsk4);
}

TEST_F(homa_socktab, homa_sock_lock_slow)
{
	mock_cycles = ~0;
	
	homa_sock_lock(&self->hsk);
	EXPECT_EQ(0, unit_get_metrics()->socket_lock_misses);
	EXPECT_EQ(0, unit_get_metrics()->socket_lock_miss_cycles);
	homa_sock_unlock(&self->hsk);
	
	mock_trylock_errors = 1;
	homa_sock_lock(&self->hsk);
	EXPECT_EQ(1, unit_get_metrics()->socket_lock_misses);
	EXPECT_NE(0, unit_get_metrics()->socket_lock_miss_cycles);
	homa_sock_unlock(&self->hsk);
}