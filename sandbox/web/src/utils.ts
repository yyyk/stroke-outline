import type { PathCommand } from './lib';

export function drawSectionTitle(ctx: CanvasRenderingContext2D, title: string) {
	ctx.fillStyle = 'black';
	ctx.font = 'bold 14px sans-serif';
	ctx.fillText(title, 0, 0);
}

export function drawLabel(
	ctx: CanvasRenderingContext2D,
	text: string,
	x: number,
	y: number,
	color = 'black',
) {
	ctx.fillStyle = color;
	ctx.font = '10px sans-serif';
	ctx.fillText(text, x, y);
}

export const Color = {
	Blue: '#4444ff',
	Red: '#ff4444',
};

export function drawPathCommands(
	ctx: CanvasRenderingContext2D,
	commands: readonly PathCommand[],
) {
	ctx.beginPath();
	for (const command of commands) {
		switch (command.type) {
			case 'moveTo':
				ctx.moveTo(command.x, command.y);
				break;
			case 'lineTo':
				ctx.lineTo(command.x, command.y);
				break;
			case 'quadTo':
				ctx.quadraticCurveTo(command.cx, command.cy, command.x, command.y);
				break;
			case 'cubicTo':
				ctx.bezierCurveTo(
					command.c1x,
					command.c1y,
					command.c2x,
					command.c2y,
					command.x,
					command.y,
				);
				break;
			case 'close':
				ctx.closePath();
				break;
		}
	}
}
