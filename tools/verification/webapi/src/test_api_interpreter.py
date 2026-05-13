import pytest

class TestInterpreters:
    
    def test_python_interpreter(self, api_client):
        """Verify Python interpreter execution."""
        
        # Check status
        status = api_client.get_python_status()
        if not status.get("available", False):
            pytest.skip("Python interpreter not available")
            
        # Exec code
        code = "print('Hello API')"
        resp = api_client.exec_python(code)
        assert resp["success"] is True
        assert "Hello API" in resp["output"]
        
        # Stop (hard to test without long running script, but checking endpoint works)
        api_client.stop_python()

    def test_lua_interpreter(self, api_client):
        """Verify Lua interpreter execution."""
        
        # Check status
        status = api_client.get_lua_status()
        if not status.get("available", False):
            pytest.skip("Lua interpreter not available")
            
        # Exec code
        code = "print('Hello API')"
        resp = api_client.exec_lua(code)
        assert resp["success"] is True
        assert "Hello API" in resp["output"]
        
        # Stop
        api_client.stop_lua()
