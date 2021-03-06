#ifndef EXECUTION_HH
#define EXECUTION_HH

/* Code for executing the building process itself.  This is by
 * far the longest source code file in Stu.  Each file or phony target
 * is represented at runtime by one Execution object.  All Execution objects
 * are allocated with new Execution(...), and are never deleted, as the
 * information contained in them needs to be cached.  All Execution objects
 * are also stored in the map called "executions_by_target" by their
 * target.  All currently active Execution objects form a rooted acyclic
 * graph.  Note that it is not a tree in the general case; executions
 * may have multiple parents.  But all nodes are reachable from the root
 * node.   
 */

#include <map> 
#include <unordered_set>

#include "job.hh"
#include "timestamp.hh"
#include "buffer.hh"

static string debug_padding= "";

class Debug_Pad 
{
public:
	Debug_Pad() 
	{
		debug_padding += "   ";
	}

	~Debug_Pad() 
	{
		debug_padding.resize(debug_padding.size() - 3);
	}
};

#define debug_padding_str  debug_padding.c_str()

class Execution
{
public:

	/* Target to build */ 
	const Target target;

	/* The instantiated file rule for this execution.  NULLPTR when there
	 * is no rule for this file (this happens for instance when a
	 * source code file is given as a dependency, or when this is a
	 * complex dependency).  Individual dynamic dependencies do have
	 * rules, in order for cycles to be detected. 
	 */ 
	shared_ptr <Rule> rule;

	/* The rule from which this execution was derived.  This is
	 * only used to detect strong cycles.  To manage the dependencies, the
	 * instantiated general rule is used.  NULLPTR if and only if RULE is
	 * NULLPTR. 
	 */ 
	shared_ptr <Rule> param_rule;

	/* Currently running executions.  Allocated with new.  Contains
	 * both dependency-subs and dynamic-subs.  
	 */
	unordered_set <Execution *> children;

	/* The parent executions */ 
	unordered_map <Execution *, Link> parents; 

	/* The job used to build this file */ 
	Job job;
	
	/* Dependencies that have not yet begun to be built.
	 * Initialized with all dependencies, and emptied over time when
	 * things are built, and filled over time when dynamic dependencies
	 * are worked on. 
	 * Entries are not necessarily unique.  The traces within the
	 * dependencies refer to the declaration of the dependencies,
	 * not to the rules of the dependencies. 
	 */ 
	Buffer buf_default;

	/* The buffer for dependencies in the second pass.  They are only started
	 * if, after (potentially) starting all non-trivial
	 * dependencies, the target must be rebuilt anyway. 
	 */
	Buffer buf_trivial; 

	/* Info about the target before it is built.  Only valid once the
	 * job was started.  Used for checking whether a file was
	 * rebuild to decide whether to remove it after a command failed or
	 * was interrupted.  The field .tv_sec is LONG_MAX when the file did
	 * not exist, or the target is not a file
	 */ 
	Timestamp timestamp_old; 

	/* Variable assignments for when the command is run.  Filled over
	 * time with the various types of variables that are possible.
	 * Note:  we use an ordered map in order to be able to output all
	 * variable assignments alphabetically; otherwise the order does not
	 * matter. 
	 */
	map <string, string> mapping_parameter; 

	/* Variable assignments to be printed when the command is executed. 
	 */
	map <string, string> mapping_variable; 

	/* Error status of this target.  The value is propagated (using '|')
	 * to the parent.  Values correspond to constants defined in
	 * error.hh; zero denotes the absence of an error. 
	 */ 
	int error;

	/* Whether this target needs to be built.  When a
	 * target is finished, this value is propagated to the parent
	 * executions (except when the F_EXISTENCE flag is set). 
	 */ 
	bool need_build;

	/* Whether we performed the check in execute().  (Only for FILE
	 * targets). 
	 */ 
	bool checked;

	/* What parts of this target have been done. Each bit represents
	 * one task done.  The depth K is equal to the dynamicity
	 * for dynamic targets, and to zero for non-dynamic targets. 
	 */
	Stack done;

	/* Latest timestamp of a (direct or indirect) file dependency that
	 * was not rebuilt.  (Files that were not rebuilt are not
	 * considered, since they make the target be rebuilt anyway.)
	 * The function execute() also changes this to consider the file
	 * itself.  This final timestamp is then carried over to the parent
	 * executions.   
	 */
	Timestamp timestamp; 

	/* Whether the file is known to exist.  
	 * -1 = no 
	 * 0  = unknown or not a T_FILE
	 * +1 = yes
	 */
	int exists;
	
	/* File, phony and dynamic targets.  
	 */ 
	Execution(Target target_,
		  Link &&link,
		  Execution *parent);

	/* Root execution.  DEPENDENCIES don't have to be unique.
	 */
	Execution(const vector <shared_ptr <Dependency> > &dependencies_); 

	~Execution(); 

	/* Whether the execution is finished working for the PARENT */ 
	bool finished(Stack avoid) const; 

	/* Whether the execution is completely finished */ 
	bool finished() const;

	/* Read dynamic dependencies from a file.  Can only be called for
	 * dynamic targets.  Called for the parent of a dynamic--file link. 
	 */ 
	void read_dynamics(Stack avoid, shared_ptr <Dependency> dependency_parent);

	/* Remove the targe file if it exists.  If OUTPUT is true, output a
	 * corresponding error message.  Return whether the file was
	 * removed.  
	 */
	bool remove_if_existing(bool output); 

	/* Start the next jobs. This will also terminate
	 * jobs when they don't need to be run anymore, and thus it can
	 * be called when K = 0 just to terminate jobs that need to be
	 * terminated.  The passed LINK.FLAG is the ORed combination
	 * of all FLAGs up the dependency chain.  
	 * Return value:  whether additional processes must be started.
	 * Can only by TRUE in random mode.  When TRUE, not all possible
	 * subjobs where started. 
	 */
 	bool execute(Execution *parent, Link &&link);

	/* Execute already-active children.  Parameters 
	 * are equivalent to those of execute(). Return value:
	 * -1: continue; 0: return false; 1: return true.
	 */ 
	int execute_children(const Link &link);

	/* Called after the job was waited for.  The PID is only passed
	 * for checking that it is correct. 
	 */
	void waited(int pid, int status); 

	/* Print full trace for the execution.  First the message is
	 * printed, then all traces for it starting at this execution. 
	 * TEXT may be "" to not print any additional message. 
	 */ 
	void print_traces(string text= "") const;

	/* Warn when the file has a modification time in the future */ 
	void warn_future_file(struct stat *buf);

	/* Note:  the top-level flags of LINK.DEPENDENCY may be
	 * modified. 
	 * Return value:  same semantics as for execute(). 
	 */
	bool deploy(const Link &link,
		    const Link &link_child);

	/* Initialize the Execution object.  Used for dynamic dependencies.
	 * Called from get_execution() before the object is connected to a
	 * new parent. */ 
	void initialize(Stack avoid);

	/* Print the command and its associated variable assignments,
	 * according to the selected verbosity level.  
	 * FILENAME_OUTPUT and FILENAME_INPUT are "" when not used. 
	 */ 
	void print_command(); 

	/* The currently running executions by process IDs */ 
	static unordered_map <pid_t, Execution *> executions_by_pid;

	/* All Execution objects.  Execution objects are never deleted.  This serves
	 * as a caching mechanism.   
	 */
	static unordered_map <Target, Execution *> executions_by_target;

	/* The timestamps for phonies.  This container serves as the
	 * "file system" for phonies, holding their timestamps. 
	 */
	static unordered_map <string, Timestamp> phonies;

	/* The timepoint of the last time wait() returned.  No file in the
	 * filesystem should be newer than this. 
	 */ 
	static Timestamp timestamp_last; 

	/* Set once before calling Execution::main().  Unchanging during the whole call
	 * to Execution::main(). 
	 */ 
	static Rule_Set rule_set; 

	/* Whether any job was started */ 
	static bool worked;

	/* Number of free slots for jobs.  This is a long because
	 * strtol() gives a long. (Parsed from the -j option)
	 */ 
	static long jobs;

	/* Propagate information from the subexecution to the execution, and
	 * then delete the child execution.  The child execution is
	 * however not deleted as it is kept for caching. 
	 */
	static void unlink(Execution *const parent, 
			   Execution *const child,
			   shared_ptr <Dependency> dependency_parent,
			   Stack avoid_parent,
			   Stack avoid_child,
			   Flags flags_child); 

	/* Get an existing execution or create a new one.
	 * Return NULLPTR when a strong cycle was found; return the execution
	 * otherwise.  PLACE is the place of where the dependency was
	 * declared.    
	 */ 
	static Execution *get_execution(const Target &target, 
					Link &&link,
					Execution *parent); 

	/* Main execution loop.  
	 */
	static int main(const vector <shared_ptr <Dependency> > &dependencies);

	/* Wait for next job to finish and finish it.  Do not start anything
	 * new.  
	 */ 
	static void wait();

	/* Find a cycle between CHILD and one of its parent executions.  This
	 * is the main entry point of the two find_cycle() functions. 
	 * A cycle is defined not in terms of
	 * filenames, but in terms of general rules, i.e., it is an error if
	 * the file "a.gz" depends on the file "a.gz.gz", when both of them
	 * came from the general rule for "$NAME.gz".  This is to make sure
	 * we don't get into infite recursion such as with:
	 *
	 * $NAME.gz:  $NAME.gz.gz { ... }
	 */ 
	static bool find_cycle(const Execution *const parent, 
			       const Execution *const child); 

	/* The helper function for find_cycle().  TRACES contains the list
	 * of traces connected CHILD to EXECUTION. 
	 */
	static bool find_cycle(const Execution *const parent, 
			       const Execution *const child,
			       vector <Trace> &traces);

	static string cycle_string(const Execution *execution);

	/* Return Nullptr when no trace should be given */ 
	static shared_ptr <Trace> cycle_trace(const Execution *child,
					      const Execution *parent);

};

unordered_map <pid_t, Execution *> Execution::executions_by_pid;
unordered_map <Target, Execution *> Execution::executions_by_target;
unordered_map <string, Timestamp> Execution::phonies;
Timestamp Execution::timestamp_last;
Rule_Set Execution::rule_set; 
bool Execution::worked= false;
long Execution::jobs= 1;

void Execution::wait() 
{
	if (option_verbose) {
		fprintf(stderr, "VERBOSE %s wait\n",
			debug_padding_str); 
	}

	assert(Execution::executions_by_pid.size()); 

	int status;
	pid_t pid= Job::wait(&status); 
	
	if (option_verbose) {
		fprintf(stderr, "VERBOSE %s wait pid = %d\n", 
			debug_padding_str,
			(int) pid);
	}

	timestamp_last= Timestamp::now(); 

	Execution *const execution= executions_by_pid.at(pid); 

	execution->waited(pid, status); 

	++jobs; 
}

bool Execution::execute(Execution *parent, Link &&link)
{
	Debug_Pad debug_pad;

	assert(jobs >= 0); 
	assert(link.avoid.get_k() == dynamic_depth(target.type)); 
	assert(done.get_k() == dynamic_depth(target.type));
	if (dynamic_depth(target.type) == 0) {
		assert(link.avoid.get_lowest() == (link.flags & ((1 << F_COUNT) - 1))); 
	}
	done.check();

	if (option_verbose) {
		string text_target= this->target.format();
		string text_flags= format_flags(link.flags);
		string text_avoid= link.avoid.format(); 

		fprintf(stderr, "VERBOSE %s %s execute %s %s\n", 
			debug_padding_str,
			text_target.c_str(),
			text_flags.c_str(),
			text_avoid.c_str()); 
	}

	/* Override the trivial flag */ 
	if (link.flags & F_OVERRIDETRIVIAL) {
		link.flags &= ~F_TRIVIAL; 
		link.avoid.rem_highest(F_TRIVIAL); 
	}

 	if (finished(link.avoid)) {
		if (option_verbose) {
			string text_target= target.format(); 
			fprintf(stderr, "VERBOSE %s %s finished\n",
				debug_padding_str,
				text_target.c_str());
		}
		return false;
	}

	/* In DFS mode, first continue the already-open children, then
	 * open new children.  In random mode, start new children first
	 * and continue already-open children second */ 

	/* 
	 * Continue the already-active child executions 
	 */  

	if (order != Order::RANDOM) {
		int ret= execute_children(link);
		if (ret >= 0)
			return ret;
	}

	/* Should children even be started?  Check whether this is an
	 * optional dependency and if it is, return when the file does not
	 * exist.  
	 */
	if ((link.flags & F_OPTIONAL) && target.type == T_FILE) {
		struct stat buf;
		int ret_stat= stat(target.name.c_str(), &buf);
		if (ret_stat < 0) {
			if (errno != ENOENT) {
				perror(target.name.c_str());
				if (! option_continue) 
					exit(ERROR_BUILD); 
				error |= ERROR_BUILD;
				done.add_neg(link.avoid); 
				exists= -1;
				return false;
			}
			done.add_highest_neg(link.avoid.get_highest()); 
			return false;
		} else {
			assert(ret_stat == 0);
			exists= +1;
		}
	}

	/* Is this a trivial dependency and we are not in trivial
	 * override mode?  Then skip the dependency. */
	if (link.flags & F_TRIVIAL) {
		done.add_neg(link.avoid);
		return false;
	}

	assert(done.get_k() == dynamic_depth(target.type));

	if (error) 
		assert(option_continue); 

	/* 
	 * Deploy dependencies (first pass), with the F_NOTRIVIAL flag
	 */ 
	while (! buf_default.empty()) {
		Link link_child= buf_default.next(); 
		Link link_child_overridetrivial= link_child;
		link_child_overridetrivial.avoid.add_highest(F_OVERRIDETRIVIAL); 
		link_child_overridetrivial.flags |= F_OVERRIDETRIVIAL; 
		buf_trivial.push(move(link_child_overridetrivial)); 
		if (deploy(link, link_child))
			return true;
		if (jobs == 0)
			return false;
	} 
	assert(buf_default.empty()); 

	if (order == Order::RANDOM) {
		int ret= execute_children(link);
		if (ret >= 0)
			return ret;
	}

	/* Some dependencies are still running */ 
	if (children.size())
		return false;

	/* There was an error in a child */ 
	if (error != 0) {
		assert(option_continue == true); 
		done.add_neg(link.avoid);
		return false;
	}

	/* Rule does not have a command.  This includes the case of dynamic
	 * executions, even though for dynamic executions the RULE variable
	 * is set (to detect cycles). */ 
	if ((target.type == T_PHONY && ! (rule != nullptr && rule->command != nullptr))
	    || target.type == T_ROOT
	    || target.type >= T_DYNAMIC) {

		done.add_neg(link.avoid);
		return false;
	}

	/* Job has already been started */ 
	if (job.started_or_waited()) {
		return false;
	}

	/* Build the file itself */ 
	assert(jobs > 0); 
	assert(target.type == T_FILE || target.type == T_PHONY); 
	assert(buf_default.empty()); 
	assert(children.empty()); 
	assert(error == 0);

	/*
	 * Check whether execution has to be built
	 */

	/* Check existence of file */
	struct stat buf;
	int ret_stat; 
	timestamp_old= Timestamp::UNDEFINED;

	const bool no_command= rule != nullptr && rule->command == nullptr;

	if (! checked && target.type == T_FILE) {

		checked= true; 

		/* We save the return value of stat() and handle errors later */ 
		ret_stat= stat(target.name.c_str(), &buf);

		/* Warn when file has timestamp in the future */ 
		if (ret_stat == 0) { 
			/* File exists */ 
			timestamp_old= Timestamp(&buf);
			if (parent == nullptr || ! (link.flags & F_EXISTENCE)) {
				warn_future_file(&buf);
			}
			exists= +1; 
		} else {
			exists= -1;
		}
 
		if (! need_build) { 
			if (ret_stat == 0) {
				/* File exists. Check whether it has to be rebuilt
				 * because of more up to date dependencies */ 

				if (timestamp.defined() && timestamp_old.older_than(timestamp)) {
					if (no_command) {
						print_warning(fmt("File target '%s' which has no command "
								  "is older than its dependency",
								  target.name));
					} else {
						need_build= true;
					}

				} else {
					timestamp= timestamp_old;
				}
			} else {
				/* Note:  Rule may be NULLPTR here for optional
				 * dependencies that do not exist and do not have a
				 * rule */

				if (errno == ENOENT) {
					/* File does not exist */

					if (! (link.flags & F_OPTIONAL)) {
						need_build= true; 
					} else {
						/* Optional dependency:  don't create the file;
						 * it will then not exist when the parent is
						 * called. 
						 */ 
						done.add_one_neg(F_OPTIONAL); 
						return false;
					}
				} else {
					/* stat() returned an actual error,
					 * e.g. permission denied:  fail */
					perror(target.name.c_str());
					if (! option_continue)
						exit(ERROR_BUILD); 
					error |= ERROR_BUILD;
					done.add_one_neg(link.avoid); 
					return false;
				}
			}
		}

		/* File does not exist, all its dependencies are up to
		 * date, and the file has no commands: that's an error. */  
		if (0 != ret_stat && no_command) {

			/* Case has already been checked, and an
			 * exception thrown */ 
			assert(errno == ENOENT); 

			if (rule->dependencies.size()) {
				print_traces
					(fmt("file without command '%s' does not exist, "
					     "although all its dependencies are up to date", 
					     target.name)); 
				explain_file_without_command_with_dependencies(); 
			} else {
				print_traces
					(fmt("file without command and without dependencies "
						 "'%s' does not exist",
						 target.name)); 
				explain_file_without_command_without_dependencies(); 
			}
			error |= ERROR_BUILD;
			done.add_one_neg(link.avoid); 
			if (! option_continue) 
				throw error;  
			return false;
		}		
	}

	if (! need_build && target.type == T_PHONY) {
		if (! phonies.count(target.name)) {
			/* Phony was not yet executed */ 
			need_build= true; 
		}
	}

	if (! need_build) {
		/* The file does not have to be built */ 
		done.add_neg(link.avoid);
		return false;
	}

	/*
	 * The command must be run now. 
	 */

	/*
	 * Re-deploy all dependencies (second pass)
	 */
	while (! buf_trivial.empty()) {
		Link link_child= buf_trivial.next(); 
		if (deploy(link, link_child))
			return true;
		if (jobs == 0)
			return false;
	} 
	assert(buf_trivial.empty()); 

	if (no_command) {
		/* A target without a command */ 
		done.add_neg(link.avoid); 
		return false;
	}

	worked= true; 
	
	if (target.type == T_PHONY) {
		Timestamp timestamp_now= Timestamp::now(); 
		assert(timestamp_now.defined()); 
		phonies[target.name]= timestamp_now; 
	}

	/* Output the command */ 
	if (rule->redirect_output)
		assert(rule->place_param_target.type == T_FILE); 
	print_command();

	/* Start the job */ 
	assert(jobs >= 1); 

	map <string, string> mapping;
	mapping.insert(mapping_parameter.begin(), mapping_parameter.end());
	mapping.insert(mapping_variable.begin(), mapping_variable.end());
	mapping_parameter.clear();
	mapping_variable.clear(); 

	const pid_t pid= job.start
		(rule->command->command, 
		 mapping,
		 rule->redirect_output 
		 ? rule->place_param_target.place_param_name.unparametrized() : "",
		 rule->filename_input.unparametrized(),
		 rule->command->place); 

	if (option_verbose) {
		string text_target= this->target.format();
		fprintf(stderr, "VERBOSE %s %s execute pid = %d\n", 
			debug_padding_str,
			text_target.c_str(),
			(int)pid); 
	}

	if (pid < 0) {
		/* Starting the job failed */ 
		print_traces(fmt("error executing command for %s", target.format())); 
		if (! option_continue) 
			exit(ERROR_BUILD); 
		error |= ERROR_BUILD;
		done.add_neg(link.avoid); 
		return false;
	}

	executions_by_pid[pid]= this;
	assert(executions_by_pid.at(pid)->job.started()); 
	assert(pid == executions_by_pid.at(pid)->job.get_pid()); 
	--jobs;
	assert(jobs >= 0);

	if (order == Order::RANDOM) {
		return jobs > 0; 
	} else if (order == Order::DFS) {
		return false;
	} else {
		assert(false);
		return false;
	}
}

int Execution::execute_children(const Link &link)
{
	/* Since unlink() may change execution->children,
	 * we must first copy it over locally, and then iterate
	 * through it */ 

	vector <Execution *> executions_children_vector
		(children.begin(), children.end()); 

	while (! executions_children_vector.empty()) {

		if (order_vec) {
			/* Exchange a random position with last position */ 
			size_t p_last= executions_children_vector.size() - 1;
			size_t p_random= random_number(executions_children_vector.size());
			if (p_last != p_random) {
				swap(executions_children_vector.at(p_last),
				     executions_children_vector.at(p_random)); 
			}
		}

		Execution *child= executions_children_vector.at
			(executions_children_vector.size() - 1);
		executions_children_vector.resize(executions_children_vector.size() - 1); 
		
		assert(child != nullptr);

		Stack avoid_child= child->parents.at(this).avoid;
		Flags flags_child= child->parents.at(this).flags;

		if (target.type == T_PHONY) { 
			flags_child |= link.flags; 
		}
		
		Link link_child(avoid_child, flags_child, child->parents.at(this).place,
				child->parents.at(this).dependency);

		if (child->execute(this, move(link_child)))
			return 1;
		assert(jobs >= 0);
		if (jobs == 0)  
			return 0;

		if (child->finished(avoid_child)) {
			unlink(this, child, 
			       link.dependency,
			       link.avoid, 
			       avoid_child, flags_child); 
		}
	}

	if (error) 
		assert(option_continue); 

	return -1;
}

void Execution::waited(int pid, int status) 
{
	assert(job.started()); 
	assert(job.get_pid() == pid); 
	assert(buf_default.empty()); 
	assert(buf_trivial.empty()); 
	assert(children.size() == 0); 

	assert(done.get_k() == 0);
	done.add_one_neg(0); 

	executions_by_pid.erase(pid); 

	/* The file may have been built, so forget that it was known to
	 * not exist */
	if (exists < 0)  
		exists= 0;
	
	if (job.waited(status, pid)) {
		/* Command was successful */ 

		/* For file targets, check that the file was built */ 
		if (target.type == T_FILE) {

			struct stat buf;

			if (0 == stat(target.name.c_str(), &buf)) {
				exists= +1;
				/* Check that file was not created with modification
				 * time in the future */  
				warn_future_file(&buf); 
				/* Check that file is not older that Stu
				 * startup */ 
				Timestamp timestamp_file(&buf);
				if (timestamp_file.older_than(Timestamp::startup)) {

					/* Check whether the file is actually a symlink, in
					 * which case we ignore that error */ 
					if (0 > lstat(target.name.c_str(), &buf)) {
						perror(target.name.c_str()); 
						error |= ERROR_BUILD;
						if (! option_continue)
							throw error; 
					}
					if (! S_ISLNK(buf.st_mode)) {
						error |= ERROR_BUILD;
						rule->place <<
							fmt("timestamp of file '%s' "
							    "after execution of its command is older than %s startup", 
							    target.name, dollar_zero);  
						print_info(fmt("Timestamp of %s is %s",
							       target.format(), timestamp_file.format()));
						print_info(fmt("Startup timestamp is %s",
							       Timestamp::startup.format())); 
						print_traces();
						if (! option_continue)
							throw error; 
					}
				}
			} else {
				error |= ERROR_BUILD; 
				rule->command->place <<
					fmt("file '%s' was not built by command", 
					    target.name); 
				print_traces();
				exists= -1;
				if (! option_continue)
					throw error; 
			}
		}

	} else {
		/* Command failed */ 

		error |= ERROR_BUILD; 

		string reason;
		if (WIFEXITED(status)) {
			reason= frmt("failed with exit code %d", WEXITSTATUS(status));
		} else if (WIFSIGNALED(status)) {
			int sig= WTERMSIG(status);
			reason= frmt("received signal %s", strsignal(sig));
		} else {
			/* This should not happen but the standard does not exclude
			 * it */ 
			reason= frmt("failed with status code %d", status); 
		}

		param_rule->command->place <<
			fmt("command for %s %s", target.format(), reason); 
		print_traces(); 

		remove_if_existing(true); 

		if (! option_continue)
			throw error; 
	}
}

int Execution::main(const vector <shared_ptr <Dependency> > &dependencies)
{
	assert(jobs >= 0);

	timestamp_last= Timestamp::now(); 

	Execution *execution_root= new Execution(dependencies); 

	int error= 0; 

	try {
		while (! execution_root->finished()) {

			Link link(Stack(), (Flags)0, Place(), shared_ptr <Dependency> ());

			bool r;

			do {
				if (option_verbose) {
					fprintf(stderr, "VERBOSE %s main.next\n", 
						debug_padding_str);
				}
				r= execution_root->execute(nullptr, move(link));
			} while (r);

			if (executions_by_pid.size()) {
				wait();
			}
		}

		assert(execution_root->finished()); 

		bool success= (execution_root->error == 0);

		if (! option_continue)
			assert(success); 

		if (worked && output_mode == Output::SHORT) {
			puts("Done");
		}
	
		if (success && ! worked) {
			puts("Nothing to be done"); 
		}

		if (! success && option_continue) {
			fprintf(stderr, "%s: *** Targets not rebuilt because of errors\n", 
					dollar_zero); 
		}

		error= execution_root->error; 
	} 

	/* A build error is only thrown when options_continue is
	 * not set */ 
	catch (int e) {

		assert(! option_continue); 
		assert(e >= 1 && e <= 3); 

		error= e; 
		
		/* Terminate all jobs */ 
		if (executions_by_pid.size()) {
			print_error("Terminating all running jobs"); 
			job_terminate_all();
		}
	}

	return error;
}

void Execution::unlink(Execution *const parent, 
		       Execution *const child,
		       shared_ptr <Dependency> dependency_parent,
		       Stack avoid_parent,
		       Stack avoid_child,
		       Flags flags_child)
{
	(void) avoid_child;

	if (option_verbose) {
		string text_parent= parent->target.format();
		string text_child= child->target.format();
		string text_done_child= child->done.format();
		fprintf(stderr, "VERBOSE %s %s unlink %s %s\n",
			debug_padding_str,
			text_parent.c_str(),
			text_child.c_str(),
			text_done_child.c_str());
	}

	assert(parent != child); 
	assert(child->finished(avoid_child)); 

	if (! option_continue)  
		assert(child->error == 0); 

	/*
	 * Propagations
	 */

	/* Propagate dynamic dependencies */ 
	if (flags_child & F_READ) {
		assert(child->target.type == T_FILE); 
		assert(T_FILE < parent->target.type);
		assert(parent->target.name == child->target.name); 
		assert(child->done.get_k() == 0); 
		
		bool do_read= true;

		if (child->error != 0) {
			do_read= false;
		}

		/* Don't read the dependencies when the target was optional and
		 * was not built */
		/* child->exists was set to +1 earlier when the optional
		 * dependency was found to exist
		 */
		else if (flags_child & F_OPTIONAL) {
			if (child->exists != +1) {
				do_read= false;
			}
		}

		if (do_read) {
			parent->read_dynamics(avoid_parent, dependency_parent); 
		}
	}

	/* Propagate timestamp.  Note:  When the parent execution has
	 * filename == "", this is unneccesary */ 
	if (! (flags_child & F_EXISTENCE) && ! (flags_child & F_READ)) {
		if (child->timestamp.defined()) {
			if (! parent->timestamp.defined()) {
				parent->timestamp= child->timestamp;
			} else {
				if (parent->timestamp.older_than(child->timestamp)) {
					parent->timestamp= child->timestamp; 
				}
			}
		}
	}

	/* Propagate variable dependencies */
	if ((flags_child & F_VARIABLE) && child->exists == +1) {

		/* Read the content of the file into a string as the
		 * variable value */  

		assert(child->target.type == T_FILE);
		string filename= child->target.name;
		int fd;
		string content;
		size_t filesize;

		struct stat buf;
		fd= open(filename.c_str(), O_RDONLY);
		if (fd < 0) {
			goto error;
		}
		if (0 > fstat(fd, &buf)) {
			goto error_fd;
		}

		filesize= buf.st_size;
		content.resize(filesize);
		if ((ssize_t)filesize != read(fd, (void *) content.c_str(), filesize)) {
			goto error_fd;
		}

		if (0 > close(fd))  
			goto error;

		/* Remove space at beginning and end of the content.
		 * The characters are exactly those used by isspace() in
		 * the C locale.  
		 */ 
		content.erase(0, content.find_first_not_of(" \n\t\f\r\v")); 
		content.erase(content.find_last_not_of(" \n\t\f\r\v") + 1);  

		parent->mapping_variable[filename]= content;

		if (0) {
		error_fd:
			close(fd); 
		error:
			parent->error |= ERROR_BUILD;
			child->param_rule->place_param_target.place <<
				fmt("generated file '%s' for variable dependency was built but cannot be found now", 
				    filename);
			child->print_traces();

			if (! option_continue)
				throw parent->error; 
		}
	}

	/*
	 * Propagate variables over phonies without commands and dynamic
	 * targets
	 */
	if (child->target.type >= T_DYNAMIC ||
	    (child->target.type == T_PHONY &&
	     child->rule != nullptr &&
	     child->rule->command == nullptr)) {
		parent->mapping_variable.insert
			(child->mapping_variable.begin(), child->mapping_variable.end()); 
	}

	/* 
	 * Propagate attributes
	 */ 

	/* Note:  propagate the flags after propagating other things, since
	 * flags can be changed by the propagations done before. 
	 */

	parent->error |= child->error; 

	if (child->need_build 
	    && ! (flags_child & F_EXISTENCE)
	    && ! (flags_child & F_READ)) {
		parent->need_build= true; 
	}

	/* 
	 * Remove the links between them 
	 */ 

	assert(parent->children.count(child) == 1); 
	parent->children.erase(child);

	assert(child->parents.count(parent) == 1);
	child->parents.erase(parent);
}

Execution::Execution(Target target_,
		     Link &&link,
		     Execution *parent)
	:  target(target_),
	   error(0),
	   need_build(false),
	   checked(false),
	   done(dynamic_depth(target_.type), 0),
	   timestamp(Timestamp::UNDEFINED),
	   exists(0)
{
	assert(target.type == T_PHONY || target.type >= T_FILE); 
	assert(parent != nullptr); 
	assert(parents.empty()); 

	/* Fill in the rules and their parameters */ 
	if (target.type == T_FILE || target.type == T_PHONY) {
		map <string, string> mapping1; 
		rule= rule_set.get(target, param_rule, mapping1); 
		mapping_parameter= mapping1;
	} else if (target.type >= T_DYNAMIC) {
		/* We must set the rule here, so cycles in the dependency graph
		 * can be detected.  Note however that the rule of dynamic
		 * dependency executions is otherwise not used */ 
		Target target_file(T_FILE, target.name);
		map <string, string> mapping_rule; 
		rule= rule_set.get(target_file, param_rule, mapping_rule); 
	}
	assert((param_rule == nullptr) == (rule == nullptr)); 

	if (option_verbose) {
		string text_target= target.format();
		string text_rule= rule->format(); 
		fprintf(stderr, "VERBOSE  %s   %s %s\n",
			debug_padding_str,
			text_target.c_str(),
			text_rule.c_str()); 
	}

	parents[parent]= link; 
	executions_by_target[target]= this;

	if (target.type < T_DYNAMIC && rule != nullptr) {
		/* There is a rule for this execution */ 
		for (auto &i:  rule->dependencies) {
			assert(i->get_place().type != Place::Type::EMPTY); 
			Link link_new(i); 
			if (option_verbose) {
				string text_target= target.format();
				string text_link_new= link_new.format(); 
				fprintf(stderr, "VERBOSE %s    %s push %s\n",
					debug_padding_str,
					text_target.c_str(),
					text_link_new.c_str()); 
			}
			buf_default.push(move(link_new)); 
		}
	} else {
		/* There is no rule */ 

		/* Whether to produce the "rule not found" error */ 
		bool rule_not_found= false;

		if (target.type == T_FILE) {
			if (! (link.flags & F_OPTIONAL)) {
				/* Check that the file is present,
				 * or make it an error */ 
				struct stat buf;
				int ret_stat= stat(target.name.c_str(), &buf);
				if (0 > ret_stat) {
					if (errno != ENOENT) {
						perror(target.name.c_str()); 
						if (! option_continue)
							exit(ERROR_BUILD); 
					}
					/* File does not exist and there is no rule for it */ 
					error |= ERROR_BUILD;
					rule_not_found= true;
				} else {
					/* File exists:  Do nothing, and there are no
					 * dependencies to build */  
					if (parent->target.type == T_ROOT) {
						/* Output this only for top-level targets, and
						 * therefore we don't need traces */ 
						printf("No rule for building '%s', but the file exists\n", 
							   target.name.c_str()); 
					} 
				}
			}
		} else if (target.type == T_PHONY) {
			rule_not_found= true;
		} else {
			assert(target.type >= T_DYNAMIC); 
		}
		
		if (rule_not_found) {
			assert(rule == nullptr); 
			print_traces(fmt("no rule to build %s", target.format()));
			error |= ERROR_BUILD;
			if (! option_continue) 
				throw error;  
			/* Even when a rule was not found, the Execution object remains
			 * in memory */  
		}
	}

}

Execution::Execution(const vector <shared_ptr <Dependency> > &dependencies_)
	:  target(T_ROOT),
	   error(0),
	   need_build(false),
	   checked(false),
	   exists(0)
{
	executions_by_target[target]= this;

	for (auto &i:  dependencies_) {
		buf_default.push(Link(i)); 
	}
}

Execution::~Execution()
{
	/* Executions are never deleted (this is a caching mechanism) */ 
	assert(false); 
}

bool Execution::finished(Stack avoid) const
{
	assert(avoid.get_k() == done.get_k());
	assert(done.get_k() == dynamic_depth(target.type));

	Flags to_do_aggregate= 0;
	
	for (unsigned j= 0;  j <= done.get_k();  ++j) {
		to_do_aggregate |= ~done.get(j) & ~avoid.get(j); 
	}

	return (to_do_aggregate & ((1 << F_COUNT) - 1)) == 0; 
}

bool Execution::finished() const 
{
	assert(done.get_k() == dynamic_depth(target.type));

	Flags to_do_aggregate= 0;
	
	for (unsigned j= 0;  j <= done.get_k();  ++j) {
		to_do_aggregate |= ~done.get(j); 
	}

	return (to_do_aggregate & ((1 << F_COUNT) - 1)) == 0; 
}

/* The declaration of this function is in job.hh */ 
void job_terminate_all() 
{
	for (auto i= Execution::executions_by_pid.begin();
	     i != Execution::executions_by_pid.end();  ++i) {

		const pid_t pid= i->first;
		assert(pid > 0); 

		/* Passing (-pid) to kill() kills the whole process
		 * group with PGID (pid).  Since we set each child
		 * process to have its PID as its process group ID,
		 * this kills the child and all its children
		 * (recursively), up to programs that change this PGID
		 * of processes, such as Stu and shells, which have to
		 * kill their children explicitly in their signal
		 * handlers.  */ 
		if (0 > kill(-pid, SIGTERM)) {
			if (errno == ESRCH) {
				/* The child process is a zombie.  This
				 * means the child process has already
				 * terminated but we haven't wait()ed
				 * for it yet. */ 
			} else {
				perror("kill"); 
				/* Note:  Don't call exit() yet; we want all
				 * children to be killed. */ 
			}
		}
	}

	bool single= Execution::executions_by_pid.size() == 1;

	bool terminated= false;

	for (auto i= Execution::executions_by_pid.begin();
	     i != Execution::executions_by_pid.end();  ++i) {

		if (i->second->remove_if_existing(single))
			terminated= true; 
	}

	if (! single && terminated)
		fprintf(stderr, "%s: *** Removing partially built files\n",
				dollar_zero); 

	/* Check that all children are terminated */ 
	for (;;) {
		int status;
		int ret= wait(&status); 
		if (ret < 0) {
			/* wait() sets errno to ECHILD when there was no
			 * child to wait for */ 
			if (errno != ECHILD) {
				perror("waitpid"); 
				exit(ERROR_SYSTEM);
			}
			
			return; 
		}
		assert(ret > 0); 
	}
}

string Execution::cycle_string(const Execution *execution)
{
	assert(execution->param_rule); 

	Target target= execution->target; 
	if (target.type >= T_DYNAMIC)
		target.type= T_FILE; 

	const Place_Param_Target &place_param_target= execution->param_rule->place_param_target;

	assert(place_param_target.type == target.type); 

	if (place_param_target.place_param_name.get_n() == 0) {
		string text= place_param_target.place_param_name.unparametrized(); 
		assert(text == target.name); 
		return target.format();
	} else {
		string t= place_param_target.place_param_name.format_bare();
		Target o(target.type, t);
		return fmt("%s instantiated as %s", o.format(), target.format()); 
	}
}

shared_ptr <Trace> Execution::cycle_trace(const Execution *child,
					  const Execution *parent)
{
	if (parent->target.type == T_ROOT)
		return shared_ptr <Trace> ();

	if (parent->target.type == T_DYNAMIC &&
		child->target.type == T_FILE &&
		parent->target.name == child->target.name)
		return shared_ptr <Trace> (); 

	const Link &link= child->parents.at((Execution *)parent); 

	return shared_ptr <Trace> 
		(new Trace(link.place,
				   fmt("%s depends on %s", 
					   cycle_string(parent),
					   cycle_string(child)))); 
}

bool Execution::find_cycle(const Execution *const parent, 
			   const Execution *const child)
{
	/* Happens when the parent is the root execution */ 
	if (parent->param_rule == nullptr)
		return false;
		
	/* Happens with files that should be there and have no rule */ 
	if (child->param_rule == nullptr)
		return false; 

	vector <Trace> traces;
	shared_ptr <Trace> trace= cycle_trace(child, parent); 
	if (trace != nullptr)
		traces.push_back(*trace); 

	return find_cycle(parent, child, traces);
}

bool Execution::find_cycle(const Execution *const parent, 
			   const Execution *const child,
			   vector <Trace> &traces)
{
	assert(parent);
	assert(child); 

	if (parent->target.type == child->target.type &&
		parent->param_rule == child->param_rule) {

		assert(traces.size() >= 1); 

		traces.rbegin()->message= fmt
			("%s: %s",
			 traces.size() == 1 
			 ? "target must not depend on itself"
			 : "cyclic dependency",
			 traces.rbegin()->message); 

		for (auto i= traces.rbegin();  i != traces.rend();  ++i) {
			i->print();
		}

		return true;
	} 

	for (auto i= parent->parents.begin();
		 i != parent->parents.end();  ++i) {

		const Execution *parent_parent= i->first; 
		assert(parent_parent != nullptr); 
			
		if (parent_parent->param_rule == nullptr)
			continue; 
		
		shared_ptr <Trace> trace_new= cycle_trace(parent, parent_parent);
		if (trace_new != nullptr)
			traces.push_back(*trace_new); 
		bool found= find_cycle(parent_parent, child, traces);
		if (trace_new != nullptr)
			traces.pop_back(); 

		if (found)
			return true;
	}

	return false; 
}

bool Execution::remove_if_existing(bool output) 
{
	if (target.type != T_FILE)  
		return false;

	bool removed= false;

	const char *filename= target.name.c_str();

	/* Remove the file if it exists.  If it is a symlink, only the
	 * symlink itself is removed, not the file it links to */ 

	struct stat buf;
	if (0 == stat(filename, &buf)) { 

		/* If the file existed before building, remove it only if it now
		 * has a newer timestamp. 
		 */

		if (! timestamp_old.defined() || timestamp_old.older_than(Timestamp(&buf))) {

			if (output) {
				fprintf(stderr, 
					"%s: *** Removing file '%s' because command failed\n",
					dollar_zero,
					filename); 
			}
			
			removed= true;

			if (0 > ::unlink(filename)) {
				perror(filename); 
			}
		}
	}

	return removed; 
}

Execution *Execution::get_execution(const Target &target, 
				    Link &&link,
				    Execution *parent)
{
	/* Set to the returned Execution object when one is found or created
	 */   
	Execution *execution= nullptr; 

	auto it= executions_by_target.find(target);

	if (it != executions_by_target.end()) {
		/* An Execution object already exists for the target */ 

		execution= it->second; 
		if (execution->parents.count(parent)) {
			/* The parent and child are already connected -- add the
			 * necessary flags */ 
			execution->parents.at(parent).add(link.avoid, 
							  link.flags);
		} else {
			/* The parent and child are not yet connected -- add the
			 * connection */ 
			execution->parents[parent]= link;
		}
		
	} else { 
		/* Create a new Execution object */ 

		execution= new Execution(target, move(link), parent);  

		assert(execution->parents.size() == 1); 
	}

	if (find_cycle(parent, execution)) {
		parent->error |= ERROR_LOGICAL;
		if (! option_continue) 
			throw parent->error;
		return nullptr;
	}

	execution->initialize(link.avoid); 

	return execution;
}

void Execution::read_dynamics(Stack avoid,
			      shared_ptr <Dependency> dependency_parent)
{
	assert(target.type >= T_DYNAMIC);
	assert(avoid.get_k() == dynamic_depth(target.type)); 

	try {
		vector <shared_ptr <Token> > tokens;
		const string filename= target.name; 
		Place place_end; 

		Parse::parse_tokens_file(tokens, false, place_end, filename);
		auto i= tokens.begin(); 
		vector <shared_ptr <Dependency> > dependencies;

		Build build(tokens, i, place_end); 
		Place_Param_Name input; /* remains empty */ 
		Place place_input; /* remains empty */ 
		build.build_expression_list(dependencies, input, place_input); 
		
		for (auto &j:  dependencies) {

			/* Check that it is unparametrized */ 
			if (! j->is_unparametrized()) {
				shared_ptr <Dependency> dep= j;
				while (dynamic_pointer_cast <Dynamic_Dependency> (dep)) {
					shared_ptr <Dynamic_Dependency> dep2= 
						dynamic_pointer_cast <Dynamic_Dependency> (dep);
					dep= dep2->dependency; 
				}
				dynamic_pointer_cast <Direct_Dependency> (dep)
					->place_param_target.place_param_name.places.at(0) <<
					fmt("dynamic dependency %s "
					    "must not contain parametrized dependencies",
					    target.format());
				Target target_file= target;
				target_file.type= T_FILE;
				print_traces(fmt("%s is declared here", 
						 target_file.format())); 
				error |= ERROR_LOGICAL; 
				if (! option_continue) 
					throw error; 
				continue; 
			}

			/* Check that there is no multiply-dynamic variable dependency */ 
			if (j->has_flags(F_VARIABLE) && target.type > T_DYNAMIC) {
				
				/* Only direct dependencies can have the F_VARIABLE flag set */ 
				assert(dynamic_pointer_cast <Direct_Dependency> (j));

				shared_ptr <Direct_Dependency> dep= 
					dynamic_pointer_cast <Direct_Dependency> (j);

				j->get_place() <<
					fmt("variable dependency $[%s] must not appear",
					    dep->place_param_target.format_mid());
				print_traces(fmt("within multiply-dynamic dependency %s", 
						 target.format())); 
				error |= ERROR_LOGICAL;
				if (!option_continue) 
					throw error; 
				continue; 
			}

			/* If the target is multiply dynamic, we cannot add phony
			 * targets to it */ 
			if (dynamic_pointer_cast <Direct_Dependency> (j)) {
				
				shared_ptr <Direct_Dependency> direct_dependency= 
					dynamic_pointer_cast <Direct_Dependency> (j); 
				
				if (direct_dependency->place_param_target.type == T_PHONY
				    && target.type > T_DYNAMIC) {
					direct_dependency->place_param_target.place <<
						fmt("phony target %s must not appear "
						    "as dynamic dependency of %s", 
						    direct_dependency
						    ->place_param_target.format(),
						    target.format());
					Target target_file= target;
					target_file.type= T_FILE;
					print_traces(fmt("%s is declared here", 
							 target_file.format())); 
					error |= ERROR_LOGICAL;
					if (! option_continue) 
						throw error; 
					continue; 
				}
			}

			/* Add the found dependencies, with one less dynamic level
			 * than the current target.  */

			shared_ptr <Dependency> dependency(j);

			vector <shared_ptr <Dynamic_Dependency> > vec;
			shared_ptr <Dependency> p= dependency_parent;
			while (dynamic_pointer_cast <Dynamic_Dependency> (p)) {
				shared_ptr <Dynamic_Dependency> dynamic_dependency= 
					dynamic_pointer_cast <Dynamic_Dependency> (p);
				vec.resize(vec.size() + 1);
				vec.at(vec.size() - 1)= dynamic_dependency;
				p= dynamic_dependency->dependency;   
			}

			Stack avoid_this= avoid;
			assert(vec.size() == avoid_this.get_k());
			avoid_this.pop(); 
			dependency->add_flags(avoid_this.get_lowest()); 
			if (dependency->get_place_existence().type == Place::Type::EMPTY)
				dependency->set_place_existence
					(vec.at(target.type - T_DYNAMIC)->get_place_existence()); 
			if (dependency->get_place_optional().type == Place::Type::EMPTY)
				dependency->set_place_optional
					(vec.at(target.type - T_DYNAMIC)->get_place_optional()); 
			if (dependency->get_place_trivial().type == Place::Type::EMPTY)
				dependency->set_place_trivial
					(vec.at(target.type - T_DYNAMIC)->get_place_trivial()); 
			for (Type k= target.type;  k > T_DYNAMIC;  --k) {
				avoid_this.pop(); 
				Flags flags_level= avoid_this.get_lowest(); 
				dependency= make_shared <Dynamic_Dependency> (flags_level, dependency); 
				dependency->set_place_existence
					(vec.at(k - T_DYNAMIC - 1)->get_place_existence()); 
				dependency->set_place_optional
					(vec.at(k - T_DYNAMIC - 1)->get_place_optional()); 
				dependency->set_place_trivial
					(vec.at(k - T_DYNAMIC - 1)->get_place_trivial()); 
			}

			assert(avoid_this.get_k() == 0); 

			buf_default.push(Link(dependency));

			/* Check that there are no input dependencies */ 
			if (! input.empty()) {
				j->get_place() <<
					fmt("dynamic dependency %s must not contain input redirection", 
					    target.format());
				Target target_file= target;
				target_file.type= T_FILE;
				print_traces(fmt("%s is declared here", target_file.format())); 
				error |= ERROR_LOGICAL; 
				if (! option_continue) 
					throw error; 
				continue; 
			}
		}
				
		if (i != tokens.end()) {
			(*i)->get_place() << "expected a dependency";
			throw ERROR_LOGICAL;
		}
	} catch (int e) {
		assert(e >= 1 && e <= 3); 
		error |= e;
		if (! option_continue) 
			throw;
	}
}

void Execution::warn_future_file(struct stat *buf)
{
	assert(target.type == T_FILE); 
	if (timestamp_last.older_than(Timestamp(buf))) {
		print_warning(fmt("'%s' has modification time in the future",
				  target.name.c_str()));
	}
}

void Execution::print_traces(string text) const
{	
	/* The following traverses the execution graph backwards until it
	 * finds the root. We always take the first found parent, which
	 * is an arbitrary choice, but it doesn't matter here *which*
	 * dependency path we point out as an error.  
	 */

	const Execution *execution= this; 

	assert(execution->target.type != T_ROOT);

	bool first= true; 

	/* If there is a rule for this target, show the message with the
	 * rule's trace, otherwise show the message with the first
	 * dependency trace */ 
	if (execution->param_rule != nullptr && text != "") {
		execution->param_rule->place << text;
		first= false;
	}

	string text_parent= execution->target.format(); 

	for (;;) {

		auto i= execution->parents.begin(); 

		if (i->first->target.type == T_ROOT) {
			if (first && text != "") {
				print_error(fmt("No rule to build %s", 
						execution->target.format())); 
			}
			break; 
		}

		string text_child= text_parent; 
		text_parent= i->first->target.format(); 

		/* Don't show [[A]]->A edges */
		if (i->second.flags & F_READ) {
			execution= i->first; 
			continue;
		}

		Place place= i->second.place;

		string msg;
		if (first && text != "") {
				msg= fmt("%s, needed by %s", text, text_parent); 
			first= false;
		} else {	
		msg= fmt("%s is needed by %s",
				 text_child, text_parent);
		}
		place << msg;
		
		execution= i->first; 
	}
}

void Execution::print_command()
{
	if (output_mode == Output::SHORT) {
		string text= target.format_bare();
		puts(text.c_str()); 
		return;
	} 

	if (output_mode < Output::SHORT)
		return; 

	/* For single-line commands, show the variables on the same line.
	 * For multi-line commands, show them on a separate line. */ 
	bool single_line= rule->command->get_lines().size() == 1;
	bool begin= true; 

	string filename_output= rule->redirect_output 
		? rule->place_param_target.place_param_name.unparametrized() : "";
	string filename_input= rule->filename_input.unparametrized(); 

	/* Redirections */
	if (filename_output != "") {
		if (! begin)
			putchar(' '); 
		begin= false;
		printf(">%s", filename_output.c_str()); 
	}
	if (filename_input != "") {
		if (! begin)
			putchar(' '); 
		begin= false;
		printf("<%s", filename_input.c_str()); 
	}

	/* Print the parameter values.  (Variable assignments are not printed) 
	 */ 
	for (auto i= mapping_parameter.begin(); i != mapping_parameter.end();  ++i) {
		string name= i->first;
		string value= i->second;
		if (! begin)
			putchar(' '); 
		begin= false;
		printf("%s=%s", name.c_str(), value.c_str());
	}

	/* Colon */ 
	if (! begin) {
		if (! single_line) 
			puts(":"); 
		else
			fputs(": ", stdout);
	}

	/* The command itself */ 
	for (auto &i:  rule->command->get_lines()) {
		puts(i.c_str()); 
	}
}

bool Execution::deploy(const Link &link,
		       const Link &link_child)
{
	if (option_verbose) {
		string text_target= this->target.format();
		string text_link_child= link_child.format(); 
		fprintf(stderr, "VERBOSE %s %s deploy %s\n",
			debug_padding_str,
			text_target.c_str(),
			text_link_child.c_str());
	}

	/* Additional flags for the child are added here */ 
	Flags flags_child= link_child.flags; 
	Flags flags_child_additional= 0; 

	int dynamic_depth= 0;
	shared_ptr <Dependency> dep= link_child.dependency;
	while (dynamic_pointer_cast <Dynamic_Dependency> (dep)) {
		dep= dynamic_pointer_cast <Dynamic_Dependency> (dep)->dependency;
		++dynamic_depth;
	}

	shared_ptr <Direct_Dependency> direct_dependency=
		dynamic_pointer_cast <Direct_Dependency> (dep);
	assert(! direct_dependency->place_param_target.place_param_name.empty()); 

	Target target_child= direct_dependency->place_param_target.unparametrized();
	assert(target_child.type == T_FILE || target_child.type == T_PHONY);

	if (dynamic_depth != 0) {
		assert(dynamic_depth > 0);

		/* Phonies in dynamic dependencies are not allowed */ 
		if (target_child.type == T_PHONY) {
			/* The dynamic dependency contains a phony,
			 * i.e., something like [@target] */ 
			error |= ERROR_LOGICAL;
			direct_dependency->place <<
				fmt("phony target %s must not appear "
				    "as dynamic dependency for target %s", 
				    direct_dependency->place_param_target.format(),
				    target.format());
			print_traces(); 

			if (! option_continue)
				throw error;
			return false;
		}
		target_child.type += dynamic_depth; 
	}

	Stack avoid_child= link_child.avoid;

	/* Flags get carried over phonies */ 
	if (target.type == T_PHONY) { 
		flags_child_additional |= link.flags; 
		avoid_child.add_highest(link.flags);
		if (link.flags & F_EXISTENCE) {
			link_child.dependency->set_place_existence
				(link.dependency->get_place_existence()); 
		}
		if (link.flags & F_OPTIONAL) {
			link_child.dependency->set_place_optional
				(link.dependency->get_place_optional()); 
		}
		if (link.flags & F_TRIVIAL) {
			link_child.dependency->set_place_trivial
				(link.dependency->get_place_trivial()); 
		}
	}
	
	Flags flags_child_new= flags_child | flags_child_additional; 

	/* '!' and '?' do not mix, even for old flags */ 
	if ((flags_child_new & F_EXISTENCE) && 
	    (flags_child_new & F_OPTIONAL)) {

		/* '!' and '?' encountered for the same target */ 

		error |= ERROR_LOGICAL;
		const Place &place_existence= 
			link_child.dependency->get_place_existence();
		const Place &place_optional= 
			link_child.dependency->get_place_optional();
		place_existence <<
			"declaration of existence-only dependency with '!'";
		place_optional <<
			"clashes with declaration of optional dependency with '?'";
		direct_dependency->place <<
			fmt("in declaration of dependency %s", 
			    target_child.format());
		print_traces();
		explain_clash(); 
		if (! option_continue)  
			throw error;
		return false;
	}

	/* Either of '!'/'?'/'&' does not mix with '$[' */
	if ((flags_child & F_VARIABLE) &&
	    (flags_child_additional & (F_EXISTENCE | F_OPTIONAL | F_TRIVIAL))) {

		error |= ERROR_LOGICAL;
		const Place &place_variable= direct_dependency->place;
		if (flags_child_additional & F_EXISTENCE) {
			const Place &place_flag= link_child.dependency->get_place_existence(); 
			place_variable << fmt("variable dependency $[%s] must not be declared "
					      "as existence-only dependency",
					      target_child.format_mid());
			place_flag << "using '!'";
		} else if (flags_child_additional & F_OPTIONAL) {
			const Place &place_flag= link_child.dependency->get_place_optional(); 
			place_variable << fmt("variable dependency $[%s] must not be declared "
					      "as optional dependency",
					      target_child.format_mid());
			place_flag << "using '?'";
		} else {
			assert(flags_child_additional & F_TRIVIAL); 
			const Place &place_flag= link_child.dependency->get_place_trivial(); 
			place_variable << fmt("variable dependency $[%s] must not be declared "
					      "as trivial dependency",
					      target_child.format_mid());
			place_flag << "using '&'";
		} 
		print_traces();
		if (! option_continue)  
			throw error; 
		return false;
	}

	flags_child= flags_child_new; 

	Execution *child= Execution::get_execution
		(target_child, 
		 Link(avoid_child,
		      flags_child,
		      direct_dependency->place,
		      link_child.dependency),
		 this);  
	if (child == nullptr) {
		/* Strong cycle was found */ 
		return false;
	}

	children.insert(child);

	Link link_child_new(avoid_child, flags_child, link_child.place, link_child.dependency); 
	
	if (child->execute(this, move(link_child_new)))
		return true;
	assert(jobs >= 0);
	if (jobs == 0)  
		return false;
			
	if (child->finished(avoid_child)) {
		unlink(this, child, 
		       link.dependency,
		       link.avoid, 
		       avoid_child, flags_child);
	}

	return false;
}

void Execution::initialize(Stack avoid) 
{
	if (target.type >= T_DYNAMIC) {

		/* This is a special dynamic target.  Add, as an initial
		 * dependency, the corresponding file.  
		 */
		Flags flags_child= avoid.get_lowest() | F_READ;

		shared_ptr <Dependency> dependency_child= make_shared <Direct_Dependency>
			(flags_child,
			 Place_Param_Target(T_FILE, Place_Param_Name(target.name)));

		buf_default.push(Link(dependency_child, flags_child, Place()));
		/* The place of the [[A]]->A links is empty, meaning it will
		 * not be output in traces. */ 
	} 
}

#endif /* ! EXECUTION_HH */
