% LRS mutex hierarchy

# Overview

This document describes how the LRS daemon manages mutexes and their hierarchy
regarding how critical the data locked are.

## List of mutexes and their hierarchy

Here are the list of mutex currently used in the LRS, ordered by which mutex
should be locked first in case multiple interact with each other, without
any possibility to separate their use:
 1. Device thread's data mutex
 2. Request's container mutex
 3. Device thread's scheduled queue mutex
 4. Device thread's tosync array mutex
 5. LRS's response queue mutex
 6. Device thread's signal mutex

However, we must consider the special cases concerning mutexes 2, 3 and 4:
depending on the context, if the request is about to be inserted, then the
request container should be locked first, otherwise one of the other two.
