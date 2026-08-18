import asyncio
import logging
import pytest
from usmp import USMPServer, USMPProtocol, USMPSession

logger = logging.getLogger(__name__)

@pytest.mark.asyncio
async def test_server_programmatic_stop():
    psk = b"this_is_a_very_secret_key_32_bytes!!"[:32]
    server = USMPServer(host="127.0.0.1", port=9333, psk=psk, protocol=USMPProtocol.TCP)

    @server.on_session
    async def handler(session: USMPSession):
        pass

    serve_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.1)

    assert server._shutdown_event is not None
    assert not serve_task.done()

    # Trigger programmatic stop
    server.stop()
    await asyncio.wait_for(serve_task, timeout=2.0)
    assert serve_task.done()
    assert serve_task.exception() is None

@pytest.mark.asyncio
async def test_server_cancellation_graceful():
    psk = b"this_is_a_very_secret_key_32_bytes!!"[:32]
    server = USMPServer(host="127.0.0.1", port=9334, psk=psk, protocol=USMPProtocol.TCP)

    @server.on_session
    async def handler(session: USMPSession):
        pass

    serve_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.1)

    assert not serve_task.done()

    # Cancel serve task directly
    serve_task.cancel()
    await asyncio.gather(serve_task, return_exceptions=True)
    assert serve_task.done()

@pytest.mark.asyncio
async def test_server_task_cleanup_on_stop():
    psk = b"this_is_a_very_secret_key_32_bytes!!"[:32]
    server = USMPServer(host="127.0.0.1", port=9335, psk=psk, protocol=USMPProtocol.TCP)

    dummy_task_ran = False

    @server.on_session
    async def handler(session: USMPSession):
        nonlocal dummy_task_ran
        dummy_task_ran = True
        await asyncio.sleep(10.0)

    serve_task = asyncio.create_task(server.serve())
    await asyncio.sleep(0.1)

    # Manually add a dummy connection task to server._conn_tasks to simulate active connection
    async def mock_conn():
        await asyncio.sleep(5.0)

    mock_task = asyncio.create_task(mock_conn())
    server._conn_tasks.add(mock_task)

    # Trigger stop
    server.stop()
    await asyncio.wait_for(serve_task, timeout=2.0)

    # Mock task should be cancelled and finished
    assert mock_task.done()
    assert mock_task.cancelled()
