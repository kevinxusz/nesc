/* This file is part of the nesC compiler.
   Copyright (C) 2002 Intel Corporation

The attached "nesC" software is provided to you under the terms and
conditions of the GNU General Public License Version 2 as published by the
Free Software Foundation.

nesC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nesC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#ifndef NESC_COMPONENT_H

extern bool component_requires;

void declare_interface_ref(interface_ref iref, declaration gparms,
			   environment genv);

void make_implicit_interface(data_declaration fndecl,
			     function_declarator fdeclarator);

void check_generic_parameter_type(location l, data_declaration gparm);

component_declaration load_component(location l, const char *name);
environment start_implementation(void);

void interface_scan(data_declaration iref, env_scanner *scan);
data_declaration interface_lookup(data_declaration iref, const char *name);

void component_functions_iterate(component_declaration c,
				 void (*iterator)(data_declaration fndecl,
						  void *data),
				 void *data);

#endif