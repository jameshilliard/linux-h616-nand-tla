/* SPDX-License-Identifier: GPL-2.0-only */
/* Excerpt from include/linux/mtd/rawnand.h at the revision in source.json.
 * Copyright (C) 2000-2010 David Woodhouse, Steven J. Hill, Thomas Gleixner. */
struct nand_subop {
	unsigned int cs;
	const struct nand_op_instr *instrs;
	unsigned int ninstrs;
	unsigned int first_instr_start_off;
	unsigned int last_instr_end_off;
};

unsigned int nand_subop_get_addr_start_off(const struct nand_subop *subop,
					   unsigned int op_id);
unsigned int nand_subop_get_num_addr_cyc(const struct nand_subop *subop,
					 unsigned int op_id);
unsigned int nand_subop_get_data_start_off(const struct nand_subop *subop,
					   unsigned int op_id);
unsigned int nand_subop_get_data_len(const struct nand_subop *subop,
				     unsigned int op_id);

/**
 * struct nand_op_parser_addr_constraints - Constraints for address instructions
 * @maxcycles: maximum number of address cycles the controller can issue in a
 *	       single step
 */
struct nand_op_parser_addr_constraints {
	unsigned int maxcycles;
};

/**
 * struct nand_op_parser_data_constraints - Constraints for data instructions
 * @maxlen: maximum data length that the controller can handle in a single step
 */
struct nand_op_parser_data_constraints {
	unsigned int maxlen;
};

/**
 * struct nand_op_parser_pattern_elem - One element of a pattern
 * @type: the instructuction type
 * @optional: whether this element of the pattern is optional or mandatory
 * @ctx: address or data constraint
 * @ctx.addr: address constraint (number of cycles)
 * @ctx.data: data constraint (data length)
 */
struct nand_op_parser_pattern_elem {
	enum nand_op_instr_type type;
	bool optional;
	union {
		struct nand_op_parser_addr_constraints addr;
		struct nand_op_parser_data_constraints data;
	} ctx;
};

#define NAND_OP_PARSER_PAT_CMD_ELEM(_opt)			\
	{							\
		.type = NAND_OP_CMD_INSTR,			\
		.optional = _opt,				\
	}

#define NAND_OP_PARSER_PAT_ADDR_ELEM(_opt, _maxcycles)		\
	{							\
		.type = NAND_OP_ADDR_INSTR,			\
		.optional = _opt,				\
		.ctx.addr.maxcycles = _maxcycles,		\
	}

#define NAND_OP_PARSER_PAT_DATA_IN_ELEM(_opt, _maxlen)		\
	{							\
		.type = NAND_OP_DATA_IN_INSTR,			\
		.optional = _opt,				\
		.ctx.data.maxlen = _maxlen,			\
	}

#define NAND_OP_PARSER_PAT_DATA_OUT_ELEM(_opt, _maxlen)		\
	{							\
		.type = NAND_OP_DATA_OUT_INSTR,			\
		.optional = _opt,				\
		.ctx.data.maxlen = _maxlen,			\
	}

#define NAND_OP_PARSER_PAT_WAITRDY_ELEM(_opt)			\
	{							\
		.type = NAND_OP_WAITRDY_INSTR,			\
		.optional = _opt,				\
	}

/**
 * struct nand_op_parser_pattern - NAND sub-operation pattern descriptor
 * @elems: array of pattern elements
 * @nelems: number of pattern elements in @elems array
 * @exec: the function that will issue a sub-operation
 *
 * A pattern is a list of elements, each element reprensenting one instruction
 * with its constraints. The pattern itself is used by the core to match NAND
 * chip operation with NAND controller operations.
 * Once a match between a NAND controller operation pattern and a NAND chip
 * operation (or a sub-set of a NAND operation) is found, the pattern ->exec()
 * hook is called so that the controller driver can issue the operation on the
 * bus.
 *
 * Controller drivers should declare as many patterns as they support and pass
 * this list of patterns (created with the help of the following macro) to
 * the nand_op_parser_exec_op() helper.
 */
struct nand_op_parser_pattern {
	const struct nand_op_parser_pattern_elem *elems;
	unsigned int nelems;
	int (*exec)(struct nand_chip *chip, const struct nand_subop *subop);
};

#define NAND_OP_PARSER_PATTERN(_exec, ...)							\
	{											\
		.exec = _exec,									\
		.elems = (const struct nand_op_parser_pattern_elem[]) { __VA_ARGS__ },		\
		.nelems = sizeof((struct nand_op_parser_pattern_elem[]) { __VA_ARGS__ }) /	\
			  sizeof(struct nand_op_parser_pattern_elem),				\
	}

/**
 * struct nand_op_parser - NAND controller operation parser descriptor
 * @patterns: array of supported patterns
 * @npatterns: length of the @patterns array
 *
 * The parser descriptor is just an array of supported patterns which will be
 * iterated by nand_op_parser_exec_op() everytime it tries to execute an
 * NAND operation (or tries to determine if a specific operation is supported).
 *
 * It is worth mentioning that patterns will be tested in their declaration
 * order, and the first match will be taken, so it's important to order patterns
 * appropriately so that simple/inefficient patterns are placed at the end of
 * the list. Usually, this is where you put single instruction patterns.
 */
struct nand_op_parser {
	const struct nand_op_parser_pattern *patterns;
	unsigned int npatterns;
};

#define NAND_OP_PARSER(...)									\
	{											\
		.patterns = (const struct nand_op_parser_pattern[]) { __VA_ARGS__ },		\
		.npatterns = sizeof((struct nand_op_parser_pattern[]) { __VA_ARGS__ }) /	\
			     sizeof(struct nand_op_parser_pattern),				\
	}

/**
 * struct nand_operation - NAND operation descriptor
 * @cs: the CS line to select for this NAND operation
 * @deassert_wp: set to true when the operation requires the WP pin to be
 *		 de-asserted (ERASE, PROG, ...)
 * @instrs: array of instructions to execute
 * @ninstrs: length of the @instrs array
 *
 * The actual operation structure that will be passed to chip->exec_op().
 */
struct nand_operation {
	unsigned int cs;
	bool deassert_wp;
	const struct nand_op_instr *instrs;
	unsigned int ninstrs;
};
