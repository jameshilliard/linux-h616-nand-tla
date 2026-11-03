// SPDX-License-Identifier: GPL-2.0-only
/* Excerpt from drivers/mtd/nand/raw/nand_base.c at the revision in source.json.
 * Copyright (C) 2000 Steven J. Hill; 2002-2006 Thomas Gleixner.
 * Function bodies and parser context below are unchanged. */
struct nand_op_parser_ctx {
	const struct nand_op_instr *instrs;
	unsigned int ninstrs;
	struct nand_subop subop;
};

/**
 * nand_op_parser_must_split_instr - Checks if an instruction must be split
 * @pat: the parser pattern element that matches @instr
 * @instr: pointer to the instruction to check
 * @start_offset: this is an in/out parameter. If @instr has already been
 *		  split, then @start_offset is the offset from which to start
 *		  (either an address cycle or an offset in the data buffer).
 *		  Conversely, if the function returns true (ie. instr must be
 *		  split), this parameter is updated to point to the first
 *		  data/address cycle that has not been taken care of.
 *
 * Some NAND controllers are limited and cannot send X address cycles with a
 * unique operation, or cannot read/write more than Y bytes at the same time.
 * In this case, split the instruction that does not fit in a single
 * controller-operation into two or more chunks.
 *
 * Returns true if the instruction must be split, false otherwise.
 * The @start_offset parameter is also updated to the offset at which the next
 * bundle of instruction must start (if an address or a data instruction).
 */
static bool
nand_op_parser_must_split_instr(const struct nand_op_parser_pattern_elem *pat,
				const struct nand_op_instr *instr,
				unsigned int *start_offset)
{
	switch (pat->type) {
	case NAND_OP_ADDR_INSTR:
		if (!pat->ctx.addr.maxcycles)
			break;

		if (instr->ctx.addr.naddrs - *start_offset >
		    pat->ctx.addr.maxcycles) {
			*start_offset += pat->ctx.addr.maxcycles;
			return true;
		}
		break;

	case NAND_OP_DATA_IN_INSTR:
	case NAND_OP_DATA_OUT_INSTR:
		if (!pat->ctx.data.maxlen)
			break;

		if (instr->ctx.data.len - *start_offset >
		    pat->ctx.data.maxlen) {
			*start_offset += pat->ctx.data.maxlen;
			return true;
		}
		break;

	default:
		break;
	}

	return false;
}

/**
 * nand_op_parser_match_pat - Checks if a pattern matches the instructions
 *			      remaining in the parser context
 * @pat: the pattern to test
 * @ctx: the parser context structure to match with the pattern @pat
 *
 * Check if @pat matches the set or a sub-set of instructions remaining in @ctx.
 * Returns true if this is the case, false ortherwise. When true is returned,
 * @ctx->subop is updated with the set of instructions to be passed to the
 * controller driver.
 */
static bool
nand_op_parser_match_pat(const struct nand_op_parser_pattern *pat,
			 struct nand_op_parser_ctx *ctx)
{
	unsigned int instr_offset = ctx->subop.first_instr_start_off;
	const struct nand_op_instr *end = ctx->instrs + ctx->ninstrs;
	const struct nand_op_instr *instr = ctx->subop.instrs;
	unsigned int i, ninstrs;

	for (i = 0, ninstrs = 0; i < pat->nelems && instr < end; i++) {
		/*
		 * The pattern instruction does not match the operation
		 * instruction. If the instruction is marked optional in the
		 * pattern definition, we skip the pattern element and continue
		 * to the next one. If the element is mandatory, there's no
		 * match and we can return false directly.
		 */
		if (instr->type != pat->elems[i].type) {
			if (!pat->elems[i].optional)
				return false;

			continue;
		}

		/*
		 * Now check the pattern element constraints. If the pattern is
		 * not able to handle the whole instruction in a single step,
		 * we have to split it.
		 * The last_instr_end_off value comes back updated to point to
		 * the position where we have to split the instruction (the
		 * start of the next subop chunk).
		 */
		if (nand_op_parser_must_split_instr(&pat->elems[i], instr,
						    &instr_offset)) {
			ninstrs++;
			i++;
			break;
		}

		instr++;
		ninstrs++;
		instr_offset = 0;
	}

	/*
	 * This can happen if all instructions of a pattern are optional.
	 * Still, if there's not at least one instruction handled by this
	 * pattern, this is not a match, and we should try the next one (if
	 * any).
	 */
	if (!ninstrs)
		return false;

	/*
	 * We had a match on the pattern head, but the pattern may be longer
	 * than the instructions we're asked to execute. We need to make sure
	 * there's no mandatory elements in the pattern tail.
	 */
	for (; i < pat->nelems; i++) {
		if (!pat->elems[i].optional)
			return false;
	}

	/*
	 * We have a match: update the subop structure accordingly and return
	 * true.
	 */
	ctx->subop.ninstrs = ninstrs;
	ctx->subop.last_instr_end_off = instr_offset;

	return true;
}

#if IS_ENABLED(CONFIG_DYNAMIC_DEBUG) || defined(DEBUG)
static void nand_op_parser_trace(const struct nand_op_parser_ctx *ctx)
{
	const struct nand_op_instr *instr;
	char *prefix = "      ";
	unsigned int i;

	pr_debug("executing subop (CS%d):\n", ctx->subop.cs);

	for (i = 0; i < ctx->ninstrs; i++) {
		instr = &ctx->instrs[i];

		if (instr == &ctx->subop.instrs[0])
			prefix = "    ->";

		nand_op_trace(prefix, instr);

		if (instr == &ctx->subop.instrs[ctx->subop.ninstrs - 1])
			prefix = "      ";
	}
}
#else
static void nand_op_parser_trace(const struct nand_op_parser_ctx *ctx)
{
	/* NOP */
}
#endif

static int nand_op_parser_cmp_ctx(const struct nand_op_parser_ctx *a,
				  const struct nand_op_parser_ctx *b)
{
	if (a->subop.ninstrs < b->subop.ninstrs)
		return -1;
	else if (a->subop.ninstrs > b->subop.ninstrs)
		return 1;

	if (a->subop.last_instr_end_off < b->subop.last_instr_end_off)
		return -1;
	else if (a->subop.last_instr_end_off > b->subop.last_instr_end_off)
		return 1;

	return 0;
}

/**
 * nand_op_parser_exec_op - exec_op parser
 * @chip: the NAND chip
 * @parser: patterns description provided by the controller driver
 * @op: the NAND operation to address
 * @check_only: when true, the function only checks if @op can be handled but
 *		does not execute the operation
 *
 * Helper function designed to ease integration of NAND controller drivers that
 * only support a limited set of instruction sequences. The supported sequences
 * are described in @parser, and the framework takes care of splitting @op into
 * multiple sub-operations (if required) and pass them back to the ->exec()
 * callback of the matching pattern if @check_only is set to false.
 *
 * NAND controller drivers should call this function from their own ->exec_op()
 * implementation.
 *
 * Returns 0 on success, a negative error code otherwise. A failure can be
 * caused by an unsupported operation (none of the supported patterns is able
 * to handle the requested operation), or an error returned by one of the
 * matching pattern->exec() hook.
 */
int nand_op_parser_exec_op(struct nand_chip *chip,
			   const struct nand_op_parser *parser,
			   const struct nand_operation *op, bool check_only)
{
	struct nand_op_parser_ctx ctx = {
		.subop.cs = op->cs,
		.subop.instrs = op->instrs,
		.instrs = op->instrs,
		.ninstrs = op->ninstrs,
	};
	unsigned int i;

	while (ctx.subop.instrs < op->instrs + op->ninstrs) {
		const struct nand_op_parser_pattern *pattern;
		struct nand_op_parser_ctx best_ctx;
		int ret, best_pattern = -1;

		for (i = 0; i < parser->npatterns; i++) {
			struct nand_op_parser_ctx test_ctx = ctx;

			pattern = &parser->patterns[i];
			if (!nand_op_parser_match_pat(pattern, &test_ctx))
				continue;

			if (best_pattern >= 0 &&
			    nand_op_parser_cmp_ctx(&test_ctx, &best_ctx) <= 0)
				continue;

			best_pattern = i;
			best_ctx = test_ctx;
		}

		if (best_pattern < 0) {
			pr_debug("->exec_op() parser: pattern not found!\n");
			return -ENOTSUPP;
		}

		ctx = best_ctx;
		nand_op_parser_trace(&ctx);

		if (!check_only) {
			pattern = &parser->patterns[best_pattern];
			ret = pattern->exec(chip, &ctx.subop);
			if (ret)
				return ret;
		}

		/*
		 * Update the context structure by pointing to the start of the
		 * next subop.
		 */
		ctx.subop.instrs = ctx.subop.instrs + ctx.subop.ninstrs;
		if (ctx.subop.last_instr_end_off)
			ctx.subop.instrs -= 1;

		ctx.subop.first_instr_start_off = ctx.subop.last_instr_end_off;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(nand_op_parser_exec_op);

static bool nand_instr_is_data(const struct nand_op_instr *instr)
{
	return instr && (instr->type == NAND_OP_DATA_IN_INSTR ||
			 instr->type == NAND_OP_DATA_OUT_INSTR);
}

static bool nand_subop_instr_is_valid(const struct nand_subop *subop,
				      unsigned int instr_idx)
{
	return subop && instr_idx < subop->ninstrs;
}

static unsigned int nand_subop_get_start_off(const struct nand_subop *subop,
					     unsigned int instr_idx)
{
	if (instr_idx)
		return 0;

	return subop->first_instr_start_off;
}

/**
 * nand_subop_get_addr_start_off - Get the start offset in an address array
 * @subop: The entire sub-operation
 * @instr_idx: Index of the instruction inside the sub-operation
 *
 * During driver development, one could be tempted to directly use the
 * ->addr.addrs field of address instructions. This is wrong as address
 * instructions might be split.
 *
 * Given an address instruction, returns the offset of the first cycle to issue.
 */
unsigned int nand_subop_get_addr_start_off(const struct nand_subop *subop,
					   unsigned int instr_idx)
{
	if (WARN_ON(!nand_subop_instr_is_valid(subop, instr_idx) ||
		    subop->instrs[instr_idx].type != NAND_OP_ADDR_INSTR))
		return 0;

	return nand_subop_get_start_off(subop, instr_idx);
}
EXPORT_SYMBOL_GPL(nand_subop_get_addr_start_off);

/**
 * nand_subop_get_num_addr_cyc - Get the remaining address cycles to assert
 * @subop: The entire sub-operation
 * @instr_idx: Index of the instruction inside the sub-operation
 *
 * During driver development, one could be tempted to directly use the
 * ->addr->naddrs field of a data instruction. This is wrong as instructions
 * might be split.
 *
 * Given an address instruction, returns the number of address cycle to issue.
 */
unsigned int nand_subop_get_num_addr_cyc(const struct nand_subop *subop,
					 unsigned int instr_idx)
{
	int start_off, end_off;

	if (WARN_ON(!nand_subop_instr_is_valid(subop, instr_idx) ||
		    subop->instrs[instr_idx].type != NAND_OP_ADDR_INSTR))
		return 0;

	start_off = nand_subop_get_addr_start_off(subop, instr_idx);

	if (instr_idx == subop->ninstrs - 1 &&
	    subop->last_instr_end_off)
		end_off = subop->last_instr_end_off;
	else
		end_off = subop->instrs[instr_idx].ctx.addr.naddrs;

	return end_off - start_off;
}
EXPORT_SYMBOL_GPL(nand_subop_get_num_addr_cyc);

/**
 * nand_subop_get_data_start_off - Get the start offset in a data array
 * @subop: The entire sub-operation
 * @instr_idx: Index of the instruction inside the sub-operation
 *
 * During driver development, one could be tempted to directly use the
 * ->data->buf.{in,out} field of data instructions. This is wrong as data
 * instructions might be split.
 *
 * Given a data instruction, returns the offset to start from.
 */
unsigned int nand_subop_get_data_start_off(const struct nand_subop *subop,
					   unsigned int instr_idx)
{
	if (WARN_ON(!nand_subop_instr_is_valid(subop, instr_idx) ||
		    !nand_instr_is_data(&subop->instrs[instr_idx])))
		return 0;

	return nand_subop_get_start_off(subop, instr_idx);
}
EXPORT_SYMBOL_GPL(nand_subop_get_data_start_off);

/**
 * nand_subop_get_data_len - Get the number of bytes to retrieve
 * @subop: The entire sub-operation
 * @instr_idx: Index of the instruction inside the sub-operation
 *
 * During driver development, one could be tempted to directly use the
 * ->data->len field of a data instruction. This is wrong as data instructions
 * might be split.
 *
 * Returns the length of the chunk of data to send/receive.
 */
unsigned int nand_subop_get_data_len(const struct nand_subop *subop,
				     unsigned int instr_idx)
{
	int start_off = 0, end_off;

	if (WARN_ON(!nand_subop_instr_is_valid(subop, instr_idx) ||
		    !nand_instr_is_data(&subop->instrs[instr_idx])))
		return 0;

	start_off = nand_subop_get_data_start_off(subop, instr_idx);

	if (instr_idx == subop->ninstrs - 1 &&
	    subop->last_instr_end_off)
		end_off = subop->last_instr_end_off;
	else
		end_off = subop->instrs[instr_idx].ctx.data.len;

	return end_off - start_off;
}
EXPORT_SYMBOL_GPL(nand_subop_get_data_len);
