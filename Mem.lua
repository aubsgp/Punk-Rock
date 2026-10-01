-- Basic queue implementation. Technically "flawed" in that head and tail slowly drift to larger and larger values. In practice, we will never have a queue that lives long enough for this to remotely matter.
local Queue = {}
Queue.__index = Queue

function Queue:new()
    return setmetatable({head = 1, tail = 1}, self)
end

function Queue:is_empty()
    return self.head == self.tail
end

function Queue:peek()
    return self[self.head]
end

function Queue:add(element)
    self[self.tail] = element
    self.tail = self.tail + 1
end

function Queue:pop()
    if not self:is_empty() then
        local ret = self[self.head]
        self[self.head] = nil
        self.head = self.head + 1
        return ret
    else
        return nil
    end
end

-- OOP wrapper for dynamic memory address pointer chasing. Each address is a node, and each node has at least one edge relating it to at least one other node. 
-- If multiple "parent" nodes exist, the node will return nil if the addresses of the parents do not resolve to the same value. This is to more robustly detect corruption and unstable memory.
local MemNode = {
    cache = {}
}
MemNode.__index = MemNode

function MemNode.clear_cache()
    MemNode.cache = {}
end

function MemNode:new(parents)
    local node = {}
    setmetatable(node, self)
    node.parents = parents
    return node
end

function MemNode:root(addr)
    return MemNode:new({{nil, addr}})
end

function MemNode:child(relation)
    return MemNode:new({{self, relation}})
end

function MemNode:add_parent(parent, relation)
    if self.parents == nil then
        self.parents = {{parent, relation}}
    else
        table.insert(self.parents, {parent, relation})
    end
end

function MemNode.sanity_check(addr)
    if addr == nil or addr < 0x02000000 or addr >= 0x02400000 then
        return false
    end
    return addr
end

-- Helper function to evaluate a given relation to a known address.
function MemNode.evaluate_edge(address, relation)
    for _, step in ipairs(relation) do
        if not MemNode.sanity_check(address) then
            return nil
        end
        if step == 0 then -- 0 = deref
            address = memory.readdword(address)
        else -- anything else is an offset.
            address = address + step
        end
    end

    return address
end

-- Helper function for construction of the "ancestor" subgraph. This graph is formatted source -> target -> indices -> True. We use indices as keys so we can cache results of traversal by edge later.
function MemNode:add_to_graph(graph)
    if graph.seen[self] then -- No repeats, we don't want any infinite loops.
        return
    else
        graph.seen[self] = true
    end

    if MemNode.cache[self] ~= nil or self.parents[1][1] == nil then -- If we are a root node, or have been cached via a previous get_address() call this frame (and should be treated as a root node)
        MemNode.cache[self] = MemNode.cache[self] or self.parents[1][2] -- cache our address (if not already cached), add ourselves to the queue and return. There are no edges to add here.
        graph.queue:add(self)
        return
    end

    for i, edge in ipairs(self.parents) do -- However, if we aren't a root node, then we should first add all our edges to the graph.
        graph[edge[1]] = graph[edge[1]] or {}
        graph[edge[1]][self] = graph[edge[1]][self] or {}
        graph[edge[1]][self][i] = true
    end


    for i, edge in ipairs(self.parents) do -- At last, make our parents do the same. This has to be done after adding all edges to make our "no repeats" check sensible.
        edge[1]:add_to_graph(graph)
    end
end

-- Helper function that checks all edges of a given node, as cached in a given graph, and returns false if they don't agree, or else the address if they do.
function MemNode:compare_addresses(graph)
    local ret = nil
    for index, edge in ipairs(self.parents) do
        local addr = graph[edge[1]][self][index]
        if addr ~= true then
            if ret == nil then
                ret = addr
            elseif ret ~= addr then
                return false
            end
        end
    end
    return ret
end

-- Basic idea: first, construct an "ancestor" subgraph, which contains all edges that can reach our target node. Then, start resolving any fully retractible edges from the bottom-up, and leave loops alone. Homotopy theorists eat your heart out.
-- This leaves a graph of only loops. Iteratively, we pick an arbitrary node (ideally not the staring node) with resolved edge(s), and "split" the graph there by passing its resolved address to all its edges. 
-- This process is repeated until every edge has resolved, at which point we return the value we have for the starting node.
-- If at any point an edge fails to resolve (by failing the sanity check) or any two edges provide different answers, we immediately panic and return false, signifying that game memory is not to be trusted this frame.
-- Thus, loops DO serve a purpose: they serve as additional checks on game memory. If we expect a loop to exist in game memory, then that loop should be self-consistent when traversed from a known node.
function MemNode:get_address()
    if MemNode.cache[self] ~= nil then
        return MemNode.cache[self]
    end

    local graph = {}
    graph.queue = Queue:new()
    graph.seen = {}
    self:add_to_graph(graph)

    local candidates = {}
    local num_candidates = 0
    while num_candidates > 0 or not graph.queue:is_empty() do
        if graph.queue:is_empty() then -- (finds the candidate with the most resolved edges and plops them into the queue. to be used when we have retracted all edges and arrived at only loops.)
            local best_candidate = nil
            for candidate, num_resolved in pairs(candidates) do
                if not best_candidate then
                    best_candidate = candidate
                else
                    if num_resolved > candidates[best_candidate] then
                        best_candidate = candidate
                    end
                end
            end

            MemNode.cache[best_candidate] = best_candidate:compare_addresses(graph)
            if not MemNode.cache[best_candidate] then
                return false
            end
            candidates[best_candidate] = nil
            num_candidates = num_candidates - 1
            graph.queue:add(best_candidate)
        end

        while not graph.queue:is_empty() do -- While our queue has elements,
            local source = graph.queue:pop() -- grab them,
            local addr = MemNode.cache[source] -- get their addresses,
            if not addr then
                return false
            end

            for target, indices in pairs(graph[source] or {}) do -- and propagate address computations to anything they're a parent of. 
                for index, _ in pairs(indices) do
                    graph[source][target][index] = MemNode.sanity_check(MemNode.evaluate_edge(addr, target.parents[index][2]))
                    if not graph[source][target][index] then
                        return false
                    end

                    if not candidates[target] then -- If the node being propagated to isn't being tracked,
                        if MemNode.cache[target] ~= nil then  -- and thats because they've already been resolved,
                            if graph[source][target][index] ~= MemNode.cache[target] then -- then we should just check if this computed address matches what we computed previously, as a safety check.
                                return false
                            end
                        else  -- Otherwise, mark the number of resolved edges.
                            candidates[target] = 1
                            num_candidates = num_candidates + 1
                        end
                    else
                        candidates[target] = candidates[target] + 1
                    end

                    if candidates[target] == #target.parents then -- If we've fully resolved a given candidate,
                        MemNode.cache[target] = target:compare_addresses(graph) -- cache its address,
                        if not MemNode.cache[target] then
                            return false
                        end
                        candidates[target] = nil -- drop it from the candidates,
                        num_candidates = num_candidates - 1
                        graph.queue:add(target) -- and add it to the queue.
                    end
                end
            end
        end
    end

    return MemNode.cache[self]
end

function MemNode:readbyte(offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    return memory.readbyte(addr + (offset or 0))
end
function MemNode:writebyte(val, offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    memory.writebyte(addr + (offset or 0), val)
    return true
end

function MemNode:readword(offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    return memory.readword(addr + (offset or 0))
end
function MemNode:writeword(val, offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    memory.writeword(addr + (offset or 0), val)
    return true
end

function MemNode:readdword(offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    return memory.readdword(addr + (offset or 0))
end
function MemNode:writedword(val, offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    memory.writedword(addr + (offset or 0), val)
    return true
end

function MemNode:readbyterange(length, offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    return memory.readbyterange(addr + (offset or 0), length)
end
function MemNode:writebyterange(bytes, offset)
    local addr = self:get_address()
    if not addr then
        return false
    end
    for i = 1, #bytes do
        memory.writebyte(addr + (offset or 0) + (i - 1), bytes[i])
    end
    return true
end

return {
    MemNode = MemNode
}