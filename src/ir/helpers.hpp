#pragma once
namespace foptim::fir {

class BasicBlock;

/*
Inserts a new basicblock redirects 'from's edges to 'to' into the new basicblock
And copies over the bb args from 'from' going to 'to' into the new basicblock terminator
@returns the new basicblock
*/
BasicBlock insert_bb_between(BasicBlock from, BasicBlock to);
// BasicBlock split_block(BasicBlock a);

} // namespace foptim::fir
