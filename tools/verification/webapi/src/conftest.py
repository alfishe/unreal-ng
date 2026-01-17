import pytest
import os
import sys
import time
from api_client import UnrealApiClient

# Ensure we can import the client
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

@pytest.fixture(scope="session")
def api_client():
    """Provides an authenticated API client instance."""
    base_url = os.environ.get("UNREAL_API_URL", "http://localhost:8090")
    client = UnrealApiClient(base_url=base_url)
    
    # Wait for API to be available
    max_retries = 5
    for i in range(max_retries):
        try:
            client.get_status()
            break
        except Exception:
            if i == max_retries - 1:
                pytest.fail("Could not connect to Unreal API server")
            time.sleep(1)
            
    return client

@pytest.fixture(scope="function")
def active_emulator(api_client):
    """Creates a temporary emulator instance for testing and cleans it up."""
    # Setup: Create and start an emulator
    # Use PENTAGON for disk tests - 48K doesn't have disk drives
    response = api_client.create_and_start_emulator(model="PENTAGON")
    emulator_id = response["id"]
    
    # Wait for it to be running
    time.sleep(0.5)
    
    yield emulator_id
    
    # Teardown: Stop and delete
    try:
        api_client.stop_emulator(emulator_id)
        # Give it a moment to stop
        time.sleep(0.5) 
        api_client.delete_emulator(emulator_id)
    except Exception as e:
        print(f"Teardown failed for emulator {emulator_id}: {e}")
