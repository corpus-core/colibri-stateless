export class EventEmitter {
    private listeners: Record<string, Array<(data: any) => void>> = {};

    public on(event: string, callback: (data: any) => void): this {
        if (!this.listeners[event]) {
            this.listeners[event] = [];
        }
        this.listeners[event].push(callback);
        return this;
    }

    public removeListener(event: string, callback: (data: any) => void): this {
        if (this.listeners[event]) {
            this.listeners[event] = this.listeners[event].filter(listener => listener !== callback);
        }
        return this;
    }

    public emit(event: string, data?: any): void {
        if (this.listeners[event]) {
            this.listeners[event].forEach(listener => {
                try {
                    listener(data);
                } catch (e) {
                    console.error(`Error in event listener for ${event}:`, e);
                }
            });
        }
    }
} 