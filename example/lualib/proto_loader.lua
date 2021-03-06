local protobuf = require("protobuf")
local moon = require("moon")
local path          = moon.path
local reg_file      = protobuf.register_file

local M={}

function M.load(pbfile)
    local filepath = path.current_directory()
    filepath = filepath.."/protocol/"..pbfile
    reg_file(filepath)
end

function M.loadall()
    local curdir = path.current_directory()
    path.traverse_folder(curdir .. "/protocol", 0, function(filepath, filetype)
        if filetype == 1 then
            if path.extension(filepath) == ".pb" then
                --printf("LoadProtocol:%s", filepath)
                reg_file(filepath)
            end
        end
        return true
    end)
end

return M
