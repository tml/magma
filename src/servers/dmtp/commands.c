
/**
 * @file /magma/servers/dmtp/commands.c
 *
 * @brief The functions involved with parsing and routing DMTP commands.
 *
 * $Author$
 * $Date$
 * $Revision$
 *
 */

#include "magma.h"
#include "commands.h"

int_t dmtp_compare(const void *compare, const void *command) {

	int_t result;
	command_t *cmd = (command_t *)command, *cmp = (command_t *)compare;

	if (!cmp->function)	result = st_cmp_ci_starts(PLACER(cmp->string, cmp->length), PLACER(cmd->string, cmd->length));
	else result = st_cmp_ci_eq(PLACER(cmp->string, cmp->length), PLACER(cmd->string, cmd->length));

	return result;
}

/**
 * @brief	Sort the DMTP command table to be ready for binary searches.
 * @return	This function returns no value.
 */
void dmtp_sort(void) {
	qsort(dmtp_commands, sizeof(dmtp_commands) / sizeof(dmtp_commands[0]), sizeof(command_t), &dmtp_compare);
	return;
}

void dmtp_requeue(connection_t *con) {

	if (!status() || con_status(con) < 0 || con->protocol.violations > con->server->violations.cutoff) {
		enqueue(&dmtp_quit, con);
	}
	else {
		enqueue(&dmtp_process, con);
	}

	return;
}

/**
 * @brief	The main entry point in the DMTP server for processing commands issued by clients.
 * @param	con		a pointer to the connection object of the client issuing the DMTP command.
 * @return	This function returns no value.
 */
void dmtp_process(connection_t *con) {

	command_t *command, client = { .function = NULL };

	// QUESTION: Is this the only comparison?
	if (con_read_line(con, false) < 0) {
		con->command = NULL;
		enqueue(&dmtp_quit, con);
		return;
	}
	else if (pl_empty(con->network.line) && ((con->protocol.spins++) + con->protocol.violations) > con->server->violations.cutoff) {
		con->command = NULL;
		enqueue(&dmtp_quit, con);
		return;
	}
	else if (pl_empty(con->network.line)) {
		con->command = NULL;
		enqueue(&dmtp_process, con);
		return;
	}

	client.string = pl_char_get(con->network.line);
	client.length = pl_length_get(con->network.line);

	if ((command = bsearch(&client, dmtp_commands, sizeof(dmtp_commands) / sizeof(dmtp_commands[0]), sizeof(command_t), dmtp_compare))) {
		con->command = command;
		con->protocol.spins = 0;

		// If the DATA and QUIT commands need control over the requeue process. If the DATA command is successful it will enqueue the
		// inbound or outbound processor instead the command processor, and the QUIT command destroys a connection thereby eliminating the need
		// to enqueue it.
		if (command->function == &dmtp_data || command->function == &dmtp_quit) {
			enqueue(command->function, con);
		}
		else {
			requeue(command->function, &dmtp_requeue, con);
		}
	}
	else {
		con->command = NULL;
		requeue(&dmtp_invalid, &dmtp_requeue, con);
	}
	return;
}
