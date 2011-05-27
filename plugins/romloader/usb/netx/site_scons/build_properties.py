# -*- coding: utf-8 -*-
#-------------------------------------------------------------------------#
#   Copyright (C) 2010 by Christoph Thelen                                #
#   doc_bacardi@users.sourceforge.net                                     #
#                                                                         #
#   This program is free software; you can redistribute it and/or modify  #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 2 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   This program is distributed in the hope that it will be useful,       #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program; if not, write to the                         #
#   Free Software Foundation, Inc.,                                       #
#   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             #
#-------------------------------------------------------------------------#


from SCons.Script import *


# Init the global build properties.
_g_build_properties = None
# Init the global environment helper
_g_env_help = None

#----------------------------------------------------------------------------
#
# Get the build properties from a file or from the command line.
#
def Read():
	global _g_build_properties
	global _g_env_help
	
	# specify the name of the file in which variables are stored
	_g_build_properties = Variables('build.properties')
	
	# Register which variables we're interested in and
	# get values from a saved file if any (defaults, which are
	# specified in the last argument, are used otherwise).
	# See http://scons.org/doc/1.2.0.d20090919/HTML/scons-user/x2378.html for details.
	_g_build_properties.Add(BoolVariable('build_debug', 'Build the project in debug mode.', 'False'))
	_g_build_properties.Add(BoolVariable('ASIC_DEBUGMSG', 'Enable debug messages.', 'False'))

	_g_env_help = Environment(variables=_g_build_properties)


#----------------------------------------------------------------------------
#
# Generate the help texts for the properties.
#
def GenerateHelp():
	global _g_build_properties
	global _g_env_help
	
	Help(_g_build_properties.GenerateHelpText(_g_env_help))


#----------------------------------------------------------------------------
#
# Show a summary of all properties
#
def PrintSummary():
	global _g_env_help
	
	print 'build properties:'
	print "\tbuild_debug = %s" % _g_env_help['build_debug']
	print "\tASIC_DEBUGMSG = %s" % _g_env_help['ASIC_DEBUGMSG']


def ApplyToEnv(env):
	global _g_env_help
	
	if _g_env_help['build_debug']==0:
		# this is the release build
		env.Append(CCFLAGS = ['-O2'])
	else:
		# this is the debug build
		env.Append(CCFLAGS = ['-O0'])
	
	if _g_env_help['ASIC_DEBUGMSG']==0:
		env.Append(CPPDEFINES = [['ASIC_DEBUGMSG', '0']])
	else:
		env.Append(CPPDEFINES = [['ASIC_DEBUGMSG', '1']])
