--
--  LuaRT tasks.wlua example
--  Concurrent Tasks that advance Progressbars
--

local ui = require "ui"

local win = ui.Window("Concurrent Tasks example", 320, 200)
local y = 30

-- Add a new Progressbar
-- and make it advance asynchronously
local function bar(i)
    ui.Label(win, "Task #"..i, 12, y)
    local pb = ui.Progressbar(win, 60, y, 230)
    y = y + 30

    -- throw a task that increase the Progressbar position
    -- uses a different priority for each task (Task #1 has lowest priority, Task #2 has higher priority)
    local task = sys.Task(function()
        while pb.position < 100 do
            sleep(75)
            pb:advance(1)
        end
    end)
    task.priority = i
    task()
end

-- Add 5 Progressbars
for i=1,5 do
    bar(i)
end

win:show()

ui.task:wait()