// $Id$
/*									tab:4
 * Copyright (c) 2004-2005 Intel Corporation
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached INTEL-LICENSE     
 * file. If you do not find these files, copies can be found by writing to
 * Intel Research Berkeley, 2150 Shattuck Avenue, Suite 1300, Berkeley, CA, 
 * 94704.  Attention:  Intel License Inquiry.
 */

package net.tinyos.nesc.dump.xml;

import org.xml.sax.*;
import java.util.*;

public class Field extends Definition
{
    static protected DefinitionTable defs;

    public String name; /* not unique */
    public String ref; /* globally unique */
    public StructureDefinition container;

    public void init(Attributes attrs) {
	ref = attrs.getValue("ref");
	name = attrs.getValue("field");
    }

    synchronized Definition define(Attribute attrs) {
	return defs.define(attrs.getValue("ref"), attrs, this);
    }

    static synchronized Definition lookup(NDReader reader, Attribute attrs) {
	return defs.lookup(reader, attrs.getValue("ref"), attrs, "field");
    }
}
