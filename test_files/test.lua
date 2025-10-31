-- Save this as test.lua
request = function()
    return wrk.format("GET", wrk.url, { ["Connection"] = "close" })
  end
  
  -- Run with:
  -- wrk -c 100 -t 4 -d 30s --script test.lua http://localhost:8080/test