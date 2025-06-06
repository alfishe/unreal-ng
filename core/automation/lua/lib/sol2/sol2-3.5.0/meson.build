project('sol2', 'cpp')

# Find lua dependency
if get_option('lua_cpp')
	lua_cpp = 'true'
else
	lua_cpp = 'false'
endif

if get_option('use_luajit')
	luadep_name = 'luajit'
	luadep_options = []
	lua_cpp = 'false'
else
	luadep_name = 'lua'
	luadep_options = [ 'lua_cpp=' + lua_cpp ]
endif
lua_dep = dependency(luadep_name, fallback: [ luadep_name, luadep_name + '_dep' ], default_options: luadep_options)

# Set compiler flags if we're compiling lua as C++.
compile_args = []

if get_option('lua_cpp')
	compile_args = [ '-DSOL_USING_CXX_LUA=1' ]
endif

# Expose standard dependency.
sol2_dep = declare_dependency(
	include_directories: include_directories('./include'),
	compile_args: compile_args,
	dependencies: [ lua_dep ],
)

# Single header targets requested.
if get_option('single')

	# Check if we have python installed (required for creating single).
	python = find_program('python3', required: false)

	if not python.found()
		python = find_program('python', required: false)
	endif

	if not python.found() 
		error('Could not locate Python. Python is required when building a single header.')
	endif

	# List all headers that the single header comprises of.
	cmd = run_command(python, 'list_headers.py')

	if cmd.returncode() != 0
		error('Could not list sol2 header files.')
	endif

	# Create our custom target to generate the single header file.
	sol2_single = custom_target('sol2_single',
		input: cmd.stdout().strip().split('\n'),
		output: 'sol.hpp',
		command: [ python, files('single/single.py'), '--input', './include', '--output', '@OUTPUT@' ]
	)

	# Expose the dependency.
	sol2_dep = declare_dependency(
		sources: [ sol2_single ],
		compile_args: compile_args,
		dependencies: [ lua_dep ],
	)
endif
