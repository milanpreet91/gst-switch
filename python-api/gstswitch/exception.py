__all__ = ['BaseError', 'PathError', 'ServerProcessError', 'ConnectionError']


class BaseError(Exception):
    """docstring for BaseError"""
    pass


class PathError(BaseError):
    """docstring for PathError"""
    pass


class ServerProcessError(BaseError):
    """docstring for ServerProcessError"""
    pass


class ConnectionError(BaseError):
    """docstring for ConnectionError"""
    pass


class ConnectionReturnError(BaseError):
    """docstring for ConnectionReturnError"""
    pass

class RangeError(BaseError):
    """docstring for RangeError"""
    pass

