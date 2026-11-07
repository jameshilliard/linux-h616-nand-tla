/* SPDX-License-Identifier: GPL-2.0-only */
/* Inserted after the shared hosted shims and extracted production bodies. */
static struct mtd_info geom;
static unsigned char *medium;
static size_t nrows, record_size;
static unsigned int calls, cases, failures, visits[4096];
static bool symmetric, baseline;
static int error_call, error_code;
static int call_errors[16];
static size_t error_after;
static size_t completed_data, completed_oob;
static FILE *trace_file;
static unsigned int trace_count;
static struct {
	uint64_t at;
	size_t data, oob, rd, ro;
	int code;
} trace_calls[4096];

static unsigned char pattern_at(size_t pos)
{
	return (pos * 37 + (pos >> 8) * 11 + (pos >> 16) * 19) % 251;
}

static void setup(unsigned int writesize, unsigned int oobsize,
		  unsigned int pages_per_block, size_t pages, bool read)
{
	assert(pages <= sizeof(visits) / sizeof(visits[0]));
	free(medium);
	nrows = pages;
	record_size = writesize + oobsize;
	medium = malloc(pages * record_size);
	assert(medium);
	for (size_t i = 0; i < pages * record_size; i++)
		medium[i] = read ? pattern_at(i) : 0xff;
	geom = (struct mtd_info) {
		.size = pages * writesize, .writesize = writesize,
		.erasesize = pages_per_block * writesize,
		.oobsize = oobsize, .oobavail = oobsize / 2,
		._read_oob = mtd_read_oob, ._write_oob = mtd_write_oob,
	};
	calls = completed_data = completed_oob = 0;
	trace_count = 0;
	memset(visits, 0, sizeof(visits));
	error_call = error_code = 0;
	memset(call_errors, 0, sizeof(call_errors));
	error_after = 0;
	assert(!live_allocs);
	alloc_calls = copy_calls = fail_alloc = fail_copy = 0;
}

static int transfer(struct mtd_info *mtd, loff_t from,
		    struct mtd_oob_ops *ops, bool read)
{
	size_t row = from / mtd->writesize, col = from % mtd->writesize;
	size_t oob_per_page = mtd_oobavail(mtd, ops);
	int ret = mtd_check_oob_ops(mtd, from, ops);

	ops->retlen = ops->oobretlen = 0;
	if (ops->stats)
		memset(ops->stats, 0, sizeof(*ops->stats));
	if (ret)
		return ret;
	if (!read && !symmetric && !ops->datbuf && ops->ooblen > oob_per_page)
		return -EINVAL;
	calls++;
	assert(calls < 10000);
	if (calls < sizeof(call_errors) / sizeof(*call_errors) && call_errors[calls]) {
		error_call = calls;
		error_code = call_errors[calls];
	}
	if (error_call == (int)calls && error_code == -EIO && !error_after)
		return -EIO;
	size_t dleft = ops->len, oleft = ops->ooblen;
	while (dleft || ((symmetric || !ops->datbuf) && oleft)) {
		if (error_call == (int)calls && error_code == -EIO &&
		    ops->retlen + ops->oobretlen >= error_after)
			return -EIO;
		size_t d = min_t(size_t, dleft, mtd->writesize - col);
		size_t o = min_t(size_t, oleft, oob_per_page);
		assert(row < nrows);
		visits[row]++;
		if (read) {
			if (d)
				memcpy(ops->datbuf + ops->retlen, medium + row * record_size + col, d);
			if (o)
				memcpy(ops->oobbuf + ops->oobretlen,
				       medium + row * record_size + mtd->writesize, o);
		} else {
			if (d)
				memcpy(medium + row * record_size + col, ops->datbuf + ops->retlen, d);
			if (o)
				memcpy(medium + row * record_size + mtd->writesize,
				       ops->oobbuf + ops->oobretlen, o);
		}
		ops->retlen += d;
		ops->oobretlen += o;
		dleft -= d;
		oleft -= o;
		col = 0;
		row++;
	}
	completed_data += ops->retlen;
	completed_oob += ops->oobretlen;
	if (error_call == (int)calls && mtd_is_bitflip_or_eccerr(error_code)) {
		assert(ops->stats);
		ops->stats->corrected_bitflips = error_code == -EUCLEAN ? 3 : 0;
		ops->stats->max_bitflips = error_code == -EUCLEAN ? 3 : 0;
		ops->stats->uncorrectable_errors = error_code == -EBADMSG ? 1 : 0;
		return error_code;
	}
	return 0;
}

static int mtd_read_oob(struct mtd_info *mtd, loff_t from, struct mtd_oob_ops *ops)
{
	unsigned int i = trace_count++;
	assert(i < sizeof(trace_calls) / sizeof(*trace_calls));
	trace_calls[i].at = from;
	trace_calls[i].data = ops->len;
	trace_calls[i].oob = ops->ooblen;
	int ret = transfer(mtd, from, ops, true);
	trace_calls[i].rd = ops->retlen;
	trace_calls[i].ro = ops->oobretlen;
	trace_calls[i].code = ret;
	return ret;
}

static int mtd_write_oob(struct mtd_info *mtd, loff_t to, struct mtd_oob_ops *ops)
{
	unsigned int i = trace_count++;
	assert(i < sizeof(trace_calls) / sizeof(*trace_calls));
	trace_calls[i].at = to;
	trace_calls[i].data = ops->len;
	trace_calls[i].oob = ops->ooblen;
	int ret = transfer(mtd, to, ops, false);
	trace_calls[i].rd = ops->retlen;
	trace_calls[i].ro = ops->oobretlen;
	trace_calls[i].code = ret;
	return ret;
}

static bool io_case(bool read, unsigned int mode, size_t start,
		    size_t dlen, size_t olen)
{
	size_t o = mode == MTD_OPS_AUTO_OOB ? geom.oobavail : geom.oobsize;
	size_t first = start / geom.writesize;
	unsigned char *data = dlen ? malloc(dlen) : NULL;
	unsigned char *oob = olen ? malloc(olen) : NULL;
	unsigned char *expected = malloc(nrows * record_size);
	assert((data || !dlen) && (oob || !olen) && expected);
	memcpy(expected, medium, nrows * record_size);
	for (size_t i = 0; i < dlen; i++) {
		data[i] = read ? 0xff : pattern_at(i + 7);
		if (!read)
			expected[((start + i) / geom.writesize) * record_size +
				 (start + i) % geom.writesize] = data[i];
	}
	for (size_t i = 0; i < olen; i++) {
		oob[i] = read ? 0xff : pattern_at(i + 971);
		if (!read)
			expected[(first + i / o) * record_size + geom.writesize + i % o] = oob[i];
	}
	int ret;
	bool good;
	size_t reported_data = 0, reported_oob = 0;
	if (read) {
		struct mtd_read_req req = {
			.start = start, .len = dlen, .ooblen = olen,
			.usr_data = (uintptr_t)data, .usr_oob = (uintptr_t)oob,
			.mode = mode,
		};
		ret = mtdchar_read_ioctl(&geom, &req);
		reported_data = req.len;
		reported_oob = req.ooblen;
		good = !ret && req.len == dlen && req.ooblen == olen;
		for (size_t i = 0; i < dlen; i++)
			good &= data[i] == expected[((start + i) / geom.writesize) * record_size +
						   (start + i) % geom.writesize];
		for (size_t i = 0; i < olen; i++)
			good &= oob[i] == expected[(first + i / o) * record_size + geom.writesize + i % o];
	} else {
		struct mtd_write_req req = {
			.start = start, .len = dlen, .ooblen = olen,
			.usr_data = (uintptr_t)data, .usr_oob = (uintptr_t)oob,
			.mode = mode,
		};
		ret = mtdchar_write_ioctl(&geom, &req);
		/* Raw NAND deliberately rejects multi-page OOB-only backend calls. */
		size_t first_oob = min_t(size_t, olen, baseline ? geom.erasesize :
					max(geom.erasesize, geom.oobsize));
		if (!baseline && first_oob < olen)
			first_oob -= first_oob % o;
		if (!symmetric && !dlen && first_oob > o) {
			assert(ret == -EINVAL && !calls);
			good = true;
		} else {
			good = !ret && !memcmp(medium, expected, nrows * record_size);
		}
	}
	for (size_t row = 0; row < nrows; row++)
		good &= visits[row] <= 1;
	if (trace_file) {
		fprintf(trace_file, "{\"width\":%u,\"spare\":%u,\"avail\":%zu,\"erase\":%u,"
			"\"size\":%" PRIu64 ",\"read\":%s,\"symmetric\":%s,\"hasData\":%s,"
			"\"start\":%zu,\"data\":%zu,\"oob\":%zu,\"code\":%d,\"good\":%s,"
			"\"reported\":[%zu,%zu],\"calls\":[",
			geom.writesize, geom.oobsize, o, geom.erasesize, geom.size,
			read ? "true" : "false", symmetric ? "true" : "false",
			dlen ? "true" : "false", start, dlen, olen, ret, good ? "true" : "false",
			reported_data, reported_oob);
		for (unsigned int i = 0; i < trace_count; i++)
			fprintf(trace_file, "%s[%" PRIu64 ",%zu,%zu,%zu,%zu,%d]", i ? "," : "",
				trace_calls[i].at, trace_calls[i].data, trace_calls[i].oob,
				trace_calls[i].rd, trace_calls[i].ro, trace_calls[i].code);
		fputs("]}\n", trace_file);
	}
	cases++;
	if (!good) {
		failures++;
		if (!baseline) {
			fprintf(stderr, "FAIL %s %s W=%u O=%u avail=%u E=%u mode=%u start=%zu data=%zu oob=%zu rc=%d calls=%u\n",
				read ? "read" : "write", symmetric ? "symmetric" : "raw-nand",
				geom.writesize, geom.oobsize, geom.oobavail, geom.erasesize,
				mode, start, dlen, olen, ret, calls);
			abort();
		}
	}
	free(data); free(oob); free(expected);
	assert(!live_allocs);
	return good;
}

static void matrix(void)
{
	const unsigned int geometries[][3] = {
		{512, 16, 16}, {2048, 128, 64}, {4096, 256, 64},
		{1024, 1152, 64}, {64, 2112, 4},
	};
	const unsigned int pages[] = {1, 2, 3, 4, 5, 55, 56, 57, 63, 64, 65, 112, 128};
	for (unsigned int g = 0; g < sizeof(geometries) / sizeof(*geometries); g++) {
		unsigned int w = geometries[g][0], o = geometries[g][1], e = geometries[g][2];
		for (unsigned int backend = 0; backend < 2; backend++) {
			symmetric = backend;
			for (unsigned int read = 0; read < 2; read++) {
				for (unsigned int mode = 0; mode <= MTD_OPS_RAW; mode++) {
					for (unsigned int p = 0; p < sizeof(pages) / sizeof(*pages); p++) {
						for (unsigned int variant = 0; variant < 7; variant++) {
							setup(w, o, e, 140, read);
							size_t per = mode == MTD_OPS_AUTO_OOB ? o / 2 : o;
							size_t start = w * 2, d = pages[p] * w, ob = pages[p] * per;
							if (variant == 1) {
								start += w / 4;
								d -= w / 2;
							} else if (variant == 2) {
								ob -= per / 2;
							} else if (variant == 3) {
								ob = per / 2;
							} else if (variant == 4) {
								ob = 0;
							} else if (variant == 5) {
								d = 0;
								ob--;
							} else if (variant == 6) {
								start += w / 2;
								d--;
								ob += per;
							}
							io_case(read, mode, start, d, ob);
						}
					}
				}
			}
		}
	}
	/* Native geometry: exceed the OOB buffer without inventing big OOB. */
	for (unsigned int read = 0; read < 2; read++) {
		symmetric = true;
		setup(2048, 128, 64, 1100, read);
		bool good = io_case(read, MTD_OPS_RAW, 2048 * 3, 0, 1025 * 128);
		assert(good != baseline);
	}
	/* A final SPI-NAND read may cover more OOB pages than data pages. */
	for (unsigned int mode = 0; mode <= MTD_OPS_RAW; mode++) {
		for (unsigned int variant = 0; variant < 3; variant++) {
			symmetric = true;
			setup(2048, 128, 64, 140, true);
			size_t per = mode == MTD_OPS_AUTO_OOB ? geom.oobavail : geom.oobsize;
			size_t pages = variant == 2 ? 65 : 3;
			size_t start = 2 * geom.writesize + (variant == 0 ? 512 : 0);
			io_case(true, mode, start, pages * geom.writesize, (pages + 2) * per);
		}
	}
}

static void errors(void)
{
	symmetric = true;
	for (unsigned int i = 0; i < 3; i++) {
		int errors[] = {-EIO, -EUCLEAN, -EBADMSG};
		setup(1024, 1152, 64, 128, true);
		unsigned char data[100 * 1024], oob[100 * 1152];
		struct mtd_read_req req = {
			.len = sizeof(data), .ooblen = sizeof(oob),
			.usr_data = (uintptr_t)data, .usr_oob = (uintptr_t)oob,
			.mode = MTD_OPS_RAW,
		};
		error_call = 2;
		error_code = errors[i];
		assert(mtdchar_read_ioctl(&geom, &req) == error_code);
		assert(req.len == completed_data && req.ooblen == completed_oob);
		if (error_code != -EIO) {
			assert(req.len == sizeof(data) && req.ooblen == sizeof(oob));
			assert(req.ecc_stats.uncorrectable_errors == (error_code == -EBADMSG));
			assert(req.ecc_stats.corrected_bitflips == (error_code == -EUCLEAN ? 3 : 0));
			assert(req.ecc_stats.max_bitflips == (error_code == -EUCLEAN ? 3 : 0));
		}
	}
	for (unsigned int read = 0; read < 2; read++) {
		for (unsigned int combined = 0; combined < 2; combined++) {
			setup(1024, 1152, 64, 4, read);
			geom.oobavail = 0;
			unsigned char data[1024], oob[1];
			struct mtd_read_req rd = {
				.len = combined ? sizeof(data) : 0,
				.usr_data = combined ? (uintptr_t)data : 0,
				.ooblen = 1, .usr_oob = (uintptr_t)oob, .mode = MTD_OPS_AUTO_OOB,
			};
			struct mtd_write_req wr = {
				.len = combined ? sizeof(data) : 0,
				.usr_data = combined ? (uintptr_t)data : 0,
				.ooblen = 1, .usr_oob = (uintptr_t)oob, .mode = MTD_OPS_AUTO_OOB,
			};
			assert((read ? mtdchar_read_ioctl(&geom, &rd) : mtdchar_write_ioctl(&geom, &wr)) == -EINVAL);
			assert(!calls && !live_allocs);
		}
	}
	for (unsigned int read = 0; read < 2; read++) {
		for (unsigned int fault = 1; fault <= 11; fault++) {
			setup(1024, 1152, 64, 128, read);
			unsigned char data[100 * 1024] = {}, oob[100 * 1152] = {};
			struct mtd_read_req rd = {
				.len = sizeof(data), .ooblen = sizeof(oob),
				.usr_data = (uintptr_t)data, .usr_oob = (uintptr_t)oob,
				.mode = MTD_OPS_RAW,
			};
			struct mtd_write_req wr = {
				.len = sizeof(data), .ooblen = sizeof(oob),
				.usr_data = (uintptr_t)data, .usr_oob = (uintptr_t)oob,
				.mode = MTD_OPS_RAW,
			};
			if (fault <= 2)
				fail_alloc = fault;
			else if (fault == 3) {
				error_call = 2;
				error_code = -EIO;
			} else {
				fail_copy = fault - 3;
			}
			int ret = read ? mtdchar_read_ioctl(&geom, &rd) : mtdchar_write_ioctl(&geom, &wr);
			if (fault <= 2)
				assert(ret == -ENOMEM && !calls);
			else if (fault == 3)
				assert(ret == -EIO && calls == 2);
			else
				assert(ret == (copy_calls >= fail_copy ? -EFAULT : 0));
			assert(!live_allocs);
		}
	}
	puts("Allocation, user-copy, backend-I/O, zero-OOB and ECC error checks passed");
}

static void partial_errors(void)
{
	unsigned int count = 0;
	for (unsigned int backend = 0; backend < 2; backend++) {
		for (unsigned int read = 0; read < 2; read++) {
			for (unsigned int nth = 1; nth <= 3; nth++) {
				for (unsigned int page = 1; page <= 2; page++) {
					symmetric = backend;
					setup(4, 5, 4, 32, read);
					unsigned char data[40] = {}, oob[50] = {};
					struct mtd_read_req rd = {
						.len = sizeof(data), .ooblen = sizeof(oob),
						.usr_data = (uintptr_t)data, .usr_oob = (uintptr_t)oob,
						.mode = MTD_OPS_RAW,
					};
					struct mtd_write_req wr = {
						.len = sizeof(data), .ooblen = sizeof(oob),
						.usr_data = (uintptr_t)data, .usr_oob = (uintptr_t)oob,
						.mode = MTD_OPS_RAW,
					};
					error_call = nth;
					error_after = page * 9;
					error_code = -EIO;
					int ret = read ? mtdchar_read_ioctl(&geom, &rd) :
						mtdchar_write_ioctl(&geom, &wr);
					assert(ret == -EIO && calls == nth);
					assert(trace_calls[nth - 1].rd == page * 4);
					assert(trace_calls[nth - 1].ro == page * 5);
					if (read) {
						assert(rd.len == (nth - 1) * 12);
						assert(rd.ooblen == (nth - 1) * 15);
					}
					for (unsigned int row = 0; row < nrows; row++)
						assert(visits[row] <= 1);
					assert(!live_allocs);
					count++;
				}
			}
		}
	}
	printf("%u partial backend failures stop without retrying or committing the failed chunk\n", count);
}

static void request_edges(void)
{
	unsigned int count = 0;
	for (unsigned int read = 0; read < 2; read++) {
		for (unsigned int variant = 0; variant < 9; variant++) {
			symmetric = true;
			setup(4, 5, 4, 32, read);
			unsigned char data[4] = {}, oob[5] = {};
			u64 start = 0, data_len = 4, oob_len = 5;
			uintptr_t dp = (uintptr_t)data, op = (uintptr_t)oob;
			int expected = 0;
			switch (variant) {
			case 0: start = geom.size; expected = -EINVAL; break;
			case 1: start = UINT64_MAX - 1; expected = -EINVAL; break;
			case 2: start = geom.size; data_len = oob_len = 0; break;
			case 3: data_len = oob_len = 0; break;
			case 4: data_len = 0; break; /* non-NULL data: legacy no-op */
			case 5: dp = 0; break; /* ignore data length with NULL pointer */
			case 6: op = 0; break;
			case 7: data_len += 1ULL << 32; oob_len += 1ULL << 32; break;
			case 8:
				if (read) geom._read_oob = NULL;
				else geom._write_oob = NULL;
				expected = -EOPNOTSUPP;
				break;
			}
			struct mtd_read_req rd = {.start=start, .len=data_len, .ooblen=oob_len,
				.usr_data=dp, .usr_oob=op, .mode=MTD_OPS_RAW};
			struct mtd_write_req wr = {.start=start, .len=data_len, .ooblen=oob_len,
				.usr_data=dp, .usr_oob=op, .mode=MTD_OPS_RAW};
			int ret = read ? mtdchar_read_ioctl(&geom, &rd) : mtdchar_write_ioctl(&geom, &wr);
			assert(ret == expected && !live_allocs);
			if (expected || variant == 2 || variant == 3 || variant == 4)
				assert(!calls);
			/* Compatibility checks, not endorsements of this old u32 ABI. */
			if (read && variant == 5)
				assert(rd.len == data_len && rd.ooblen == 5);
			if (read && variant == 7)
				assert(rd.len == data_len && rd.ooblen == oob_len);
			count++;
		}
	}
	printf("%u request overflow, EOF, NULL-buffer and legacy length-mask checks passed\n", count);
}

static void ecc_sequences(void)
{
	/* Four chunks, with every arrangement of clean, corrected and bad ECC. */
	for (unsigned int seq = 0; seq < 81; seq++) {
		setup(4, 5, 4, 32, true);
		symmetric = true;
		unsigned char data[40], oob[50];
		unsigned int value = seq, bad = 0, corrected = 0;
		for (unsigned int i = 1; i <= 4; i++) {
			unsigned int kind = value % 3;
			value /= 3;
			call_errors[i] = kind == 1 ? -EUCLEAN : kind == 2 ? -EBADMSG : 0;
			bad += kind == 2;
			corrected += kind == 1;
		}
		struct mtd_read_req req = {.len=40, .ooblen=50, .usr_data=(uintptr_t)data,
			.usr_oob=(uintptr_t)oob, .mode=MTD_OPS_RAW};
		int ret = mtdchar_read_ioctl(&geom, &req);
		assert(ret == (bad ? -EBADMSG : corrected ? -EUCLEAN : 0));
		assert(req.len == 40 && req.ooblen == 50 && calls == 4);
		assert(req.ecc_stats.uncorrectable_errors == bad);
		assert(req.ecc_stats.corrected_bitflips == corrected * 3);
		assert(req.ecc_stats.max_bitflips == (corrected ? 3 : 0));
		assert(!live_allocs);
	}
	puts("81 four-chunk ECC sequences preserve counts, maximum and error precedence");
}

#if CHECK_FIXED
static void lengths(void)
{
	unsigned int checked = 0;
	for (unsigned int w = 2; w <= 2048; w *= 2) {
		for (unsigned int o = 1; o < 4096; o = o * 2 + 1) {
			for (unsigned int pages = 1; pages <= 64; pages *= 4) {
				struct mtd_info mtd = {.writesize = w, .oobsize = o, .erasesize = pages * w};
				const size_t ds[] = {1, w - 1, w, w + 1, mtd.erasesize - 1,
						    mtd.erasesize, mtd.erasesize + w};
				for (size_t col = 0; col < w; col++) {
					for (unsigned int d = 0; d < sizeof(ds) / sizeof(*ds); d++) {
						u64 cover = (col + ds[d] + w - 1) / w;
						u64 total_oob = cover * o;
						u64 start = (u64)UINT32_MAX * w + col;
						struct mtd_oob_ops ops = {
							.mode = MTD_OPS_RAW, .len = min_t(size_t, ds[d], mtd.erasesize),
							.ooblen = min_t(size_t, total_oob, max(mtd.erasesize, o)),
						};
						if (ops.len == mtd.erasesize)
							ops.len -= mtd_mod_by_ws(start + ops.len, &mtd);
						assert(!mtdchar_adjust_io_lengths(&mtd, start, &ops, total_oob));
						adjust_oob_length(&mtd, start, &ops);
						assert(ops.len && ops.len <= ds[d]);
						assert(ops.ooblen == ((col + ops.len + w - 1) / w) * o);
						assert(ops.ooblen <= total_oob);
						if (ops.len < ds[d])
							assert((col + ops.len) % w == 0);
						checked++;
					}
				}
			}
		}
	}
	printf("%u chunk-length arithmetic cases passed, including large page indices\n", checked);
	/* u64 product: the mathematical page/OOB capacity exceeds UINT32_MAX. */
	struct mtd_info mtd = {.writesize = 4096, .oobsize = 65536};
	struct mtd_oob_ops ops = {.mode=MTD_OPS_RAW, .len=UINT32_MAX, .ooblen=UINT32_MAX};
	adjust_oob_length(&mtd, (u64)UINT32_MAX * 4096, &ops);
	assert(ops.ooblen == UINT32_MAX);
}
#endif

int main(int argc, char **argv)
{
	assert(argc >= 1 && argc <= 3);
	baseline = argc == 1 ? !CHECK_FIXED : !strcmp(argv[1], "baseline");
	if (argc != 2) {
		trace_file = fopen(argc == 1 ? "calls.jsonl" : argv[2], "w");
		assert(trace_file);
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	matrix();
	printf("%u complete-I/O cases, %u mismatches%s\n", cases, failures,
	       baseline ? " (baseline reproductions)" : "");
	if (!baseline) {
		errors();
		partial_errors();
		request_edges();
		ecc_sequences();
	} else
		assert(failures);
#if CHECK_FIXED
	lengths();
#endif
	free(medium);
	if (trace_file)
		assert(!fclose(trace_file));
	puts("MTDCHAR CHUNK CHECKS PASSED");
	return 0;
}
