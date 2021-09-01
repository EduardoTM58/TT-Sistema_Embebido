//------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> 		//Para manejo de cadenas
#include <wiringPi.h> 		//para hacer uso de GPIOS
#include <time.h> 			//para obtener obtenerHoraFecha
#include <fcntl.h>			//Para modulo IoT
#include <termios.h>		//Para modulo IoT
#include "imagen.h"		  	//Manejo de imagenes .bmp
#include <sys/types.h>     	//Para hacer uso función iniDemonio();
#include <sys/stat.h>      	//Para hacer uso función iniDemonio();
#include <signal.h>        	//Para hacer uso función iniDemonio();
#include <syslog.h>        	//Para hacer uso función iniDemonio();
#include <math.h>
//------------------------------------------------------------------------------------
#define SENSOR 	25	//Terminal GPIO26 pin 37
#define EVER  	1
//------------------------------------------------------------------------------------
void obtenerHoraFecha( char fechayhora[]);
void enviarAlerta (char fechayhora[], int fd_serie);
int config_serial( char *dispositivo_serial, speed_t baudios);
unsigned char * reservarMemoria( int tamImagen );
void RGBToGray( unsigned char *imagenRGB, unsigned char *imagenGray, uint32_t width, uint32_t height );
void GrayToRGB( unsigned char *imagenRGB, unsigned char *imagenGray, uint32_t width, uint32_t height );
long int Sustraccion(unsigned char *imagenGrayBase, unsigned char *imagenGrayMov, uint32_t width, uint32_t height );
void filtroPasabajas(unsigned char *imagenGray,unsigned char *imagenFiltrada, uint32_t width, uint32_t height );
unsigned char *calcularKernelGauss(int *factor, int dim, float des);
char leerBloqueo();
long int leerPreferencia();
long int leerTiempoMed();
void iniDemonio();
//------------------------------------------------------------------------------------
unsigned char *imagenResultado;
int main(void)
{
	int fd_serie;
	int valor, control= 0;
	long int tamCambio, tiempoMed;
	char fechayhora[20], bloqueo;
	bmpInfoHeader infoBase, infoMov;
	unsigned char *imagenRGBBase, *imagenRGBMov;
	unsigned char *imagenGrayBase, *imagenGrayMov;
	unsigned char *imagenFiltroBase, *imagenFiltroMov;
	unsigned char *imagenRGBFinal;
	openlog( "Sensor", LOG_NDELAY | LOG_PID, LOG_LOCAL0 );	
	iniDemonio();

	fd_serie = config_serial( "/dev/ttyACM1", B9600 );
	wiringPiSetup();
  	pinMode (SENSOR, INPUT) ;

	for( ;EVER; )
	{
		valor = digitalRead(SENSOR);

		if ((valor == 1) && (control == 0))
		{
			bloqueo = leerBloqueo();
			//printf("Var Bloqueo=%c\n",bloqueo);
			if (bloqueo == 'd')
			{
				// Se captura la foto con la camara
				system("sudo /etc/init.d/motion stop");
      			system("raspistill -n -t 500 -e bmp -w 640 -h 480 -o /home/pi/fotosCamara/fotoMov.bmp");
	      		system("sudo /etc/init.d/motion start");
      			// Abrimos fotoBase y fotoMov
	      		imagenRGBBase = abrirBMP("/home/pi/fotosCamara/fotoBase.bmp", &infoBase);
				imagenRGBMov = abrirBMP("/home/pi/fotosCamara/fotoMov.bmp", &infoMov);
				imagenGrayBase = reservarMemoria(infoBase.width*infoBase.height);
				imagenGrayMov = reservarMemoria(infoMov.width*infoMov.height);
				imagenFiltroBase = reservarMemoria(infoBase.width*infoBase.height);
				imagenFiltroMov = reservarMemoria(infoMov.width*infoMov.height);
				imagenResultado = reservarMemoria(infoBase.width*infoBase.height);
				imagenRGBFinal = reservarMemoria(infoBase.width*infoBase.height*3);
				memset(imagenResultado, 255, infoBase.width*infoBase.height);
				//displayInfo(&infoBase);
				//displayInfo(&infoMov);
				RGBToGray(imagenRGBBase, imagenGrayBase, infoBase.width, infoBase.height);
				RGBToGray(imagenRGBMov, imagenGrayMov, infoMov.width, infoMov.height);
				filtroPasabajas(imagenGrayBase, imagenFiltroBase, infoBase.width, infoBase.height );
				filtroPasabajas(imagenGrayMov, imagenFiltroMov, infoMov.width, infoMov.height);
				tamCambio = Sustraccion(imagenFiltroBase, imagenFiltroMov, infoBase.width, infoBase.height);
				GrayToRGB(imagenRGBFinal, imagenResultado, infoBase.width, infoBase.height);
				guardarBMP("/home/pi/fotosCamara/imagenResultado.bmp", &infoBase, imagenRGBFinal);
				//printf("TamCambio: (%ld)\n",tamCambio);
				//printf("TamTotal: (%d)\n",infoBase.width*infoBase.height);
				syslog(LOG_INFO, "TamCambio: %ld", tamCambio);
				long int tamBase  = leerPreferencia();

				if (tamCambio > tamBase)
				{
					//printf("Movimiento = %d\n\n",valor);
					syslog(LOG_INFO, "Movimieto: %d", valor);
					obtenerHoraFecha(fechayhora);
					enviarAlerta(fechayhora, fd_serie);
					control = 1;
				}
				free( imagenGrayBase );
				free( imagenGrayMov );
				free( imagenResultado );
			}
		}
		else if(valor == 0)
		{
			control = 0;
			//printf("Movimiento = %d\n",valor);
			syslog(LOG_INFO, "Movimiento: %d", valor);
		}
		tiempoMed = leerTiempoMed();
		usleep(tiempoMed);
	}
	closelog();
	return 0;
}
//------------------------------------------------------------------------------------
void obtenerHoraFecha(char *fechayhora)
{
   time_t tiempo;
   struct tm *tiempoLocal;
   tiempo = time(NULL);
   tiempoLocal = localtime(&tiempo);
   strftime(fechayhora, 100, "%d/%m/%Y %H:%M:%S", tiempoLocal);
  // printf ("Fecha y hora: %s\n", fechayhora);
}

void enviarAlerta( char *fechayhora, int  fd_serie)
{
	char dato [3];
	//cadenas con comandos AT para el envio de mensajes SMS
	char CMGF[] = "AT+CMGF=1\r\n";
	char CMGL[] = "AT+CMGL=\"ALL\"\r\n";
	char CMGS[] = "AT+CMGS=\"5525185487\"\r\n";
//	char CMGS[] = "AT+CMGS=\"5524958526\"\r\n";
	char alerta[70] = "MOVIMIENTO DETECTADO ";
	strcat(fechayhora, "\x1A\r\n");
	strcat(alerta, fechayhora);
	//Mandar y recibir datos al UART
	write( fd_serie, &CMGF, strlen(CMGF) );
	read( fd_serie, dato, 2 );
	write( fd_serie, &CMGL, strlen(CMGL) );
	read( fd_serie, dato, 2);
	write( fd_serie, &CMGS, strlen(CMGS) );
	read( fd_serie, dato, 2);
	write( fd_serie, alerta, strlen(alerta) );
	read( fd_serie, dato, 2);

	syslog(LOG_INFO, "Alerta enviada");
}

char leerBloqueo()
{
	FILE *apArch;
	char valor;
	apArch = fopen("/home/pi/Variables/varBloqueo.txt", "r");
	if (apArch ==NULL)
	{
		perror("Error al abrir el Archivo");
		exit(EXIT_FAILURE);
	}
	fscanf(apArch, "%c" ,&valor);
	fclose(apArch);
	return valor;
}
long int leerPreferencia()
{
	FILE *apArch;
	char prefer;
	long int tamBase;
	apArch = fopen("/home/pi/Variables/varPreferencias.txt", "r");
	if (apArch ==NULL)
	{
		perror("Error al abrir el Archivo");
		exit(EXIT_FAILURE);
	}
	fscanf(apArch, "%c" ,&prefer);
	fclose(apArch);
	//printf("Var Preferencia=%c\n",prefer);
	if(prefer == 'n')
	{
		tamBase = 307200; //  tamaño completo de imagen
	}
	else if(prefer == 'o')
	{
		tamBase = 50000; // octava parte de la imagen
	}
	else if(prefer == 'i')
	{
		tamBase = 10000;
	}
	return tamBase;
}


long int leerTiempoMed()
{
	FILE *apArch;
	char prefert;
	long int tiempo;
	apArch = fopen("/home/pi/Variables/varTiempoMed.txt", "r");
	if (apArch ==NULL)
	{
		perror("Error al abrir el Archivo");
		exit(EXIT_FAILURE);
	}
	fscanf(apArch, "%c" ,&prefert);
	fclose(apArch);
	//printf("Var Preferencia=%c\n",prefer);
	if(prefert == 'q')
	{
		tiempo = 250000; //  microsegundos = 250 ms
	}
	else if(prefert == 'w')
	{
		tiempo = 500000; // microsegundos = 500 ms
	}
	else if(prefert == 't')
	{
		tiempo = 1000000; // microsegundos = 1 s
	}
	else if(prefert == 'y')
	{
		tiempo = 2000000;  // microsegundos = 2 s
	}
	return tiempo;
}


int config_serial( char *dispositivo_serial, speed_t baudios )
{
	struct termios newtermios;
  	int fd;
/*
 * Se abre un descriptor de archivo para manejar la interfaz serie
 * O_RDWR - Se abre el descriptor para lectura y escritura
 * O_NOCTTY - El dispositivo terminal no se convertira en el terminal del proceso
 * ~O_NONBLOCK - Se hace bloqueante la lectura de datos
 */
  	fd = open( dispositivo_serial, (O_RDWR | O_NOCTTY) & ~O_NONBLOCK );
	if( fd == -1 )
	{
		printf("Error al abrir el dispositivo tty \n");
		exit( EXIT_FAILURE );
  	}
/*
 * cflag - Proporciona los indicadores de modo de control
 *	CBAUD	- Velocidad de transmision en baudios.
 * 	CS8	- Especifica los bits por dato, en este caso 8
 * 	CLOCAL 	- Ignora las lineas de control del modem: CTS y RTS
 * 	CREAD  	- Habilita el receptor del UART
 * iflag - proporciona los indicadores de modo de entrada
 * 	IGNPAR 	- Ingnora errores de paridad, es decir, comunicación sin paridad
 * oflag - Proporciona los indicadores de modo de salida
 * lflag - Proporciona indicadores de modo local
 * 	TCIOFLUSH - Elimina datos recibidos pero no leidos, como los escritos pero no transmitidos
 * 	~ICANON - Establece modo no canonico, en este modo la entrada esta disponible inmediatamente
 * cc[]	 - Arreglo que define caracteres especiales de control
 *	VMIN > 0, VTIME = 0 - Bloquea la lectura hasta que el numero de bytes (1) esta disponible
 */
	newtermios.c_cflag 	= CBAUD | CS8 | CLOCAL | CREAD;
  	newtermios.c_iflag 	= IGNPAR;
  	newtermios.c_oflag	= 0;
  	newtermios.c_lflag 	= TCIOFLUSH | ~ICANON;
  	newtermios.c_cc[VMIN]	= 1;
  	newtermios.c_cc[VTIME]	= 0;
// Configura la velocidad de salida del UART
  	if( cfsetospeed( &newtermios, baudios ) == -1 )
	{
		printf("Error al establecer velocidad de salida \n");
		exit( EXIT_FAILURE );
  	}
// Configura la velocidad de entrada del UART
	if( cfsetispeed( &newtermios, baudios ) == -1 )
	{
		printf("Error al establecer velocidad de entrada \n" );
		exit( EXIT_FAILURE );
	}
// Limpia el buffer de entrada
	if( tcflush( fd, TCIFLUSH ) == -1 )
	{
		printf("Error al limpiar el buffer de entrada \n" );
		exit( EXIT_FAILURE );
	}
// Limpia el buffer de salida
	if( tcflush( fd, TCOFLUSH ) == -1 )
	{
		printf("Error al limpiar el buffer de salida \n" );
		exit( EXIT_FAILURE );
	}
/*
 * Se establece los parametros de terminal asociados con el
 * descriptor de archivo fd utilizando la estructura termios
 * TCSANOW - Cambia los valores inmediatamente
 */
	if( tcsetattr( fd, TCSANOW, &newtermios ) == -1 )
	{
		printf("Error al establecer los parametros de la terminal \n" );
		exit( EXIT_FAILURE );
	}
//Retorna el descriptor de archivo
	return fd;
}
//------------------------------------------------------------------------------------
void filtroPasabajas( unsigned char *imagenGray, unsigned char *imagenFiltrada, uint32_t width, uint32_t height )
{
	unsigned char * kernelGauss;
 	int factor;
 	float des = 1;
	register int x, y, xb, yb;
	int dim = 7, indice;

 	int centro, valorAux, indicek;
 	kernelGauss = calcularKernelGauss( &factor, dim, des );
 	centro = dim>>1;
	for ( y = 0; y <= (height-dim); y++ ) 
	{
		for ( x = 0; x <= (width-dim); x++ )
		{
			valorAux = 0;
			indicek = 0;
			for ( yb = 0; yb < dim; yb++ ) 
			{
				for ( xb = 0; xb < dim; xb++ ) 
				{
					indice   =  ((y+yb) * width) + (x+xb);
					valorAux +=  kernelGauss[ indicek++ ] * imagenGray[ indice ];
				}
			}
			valorAux = valorAux/factor;

			indice   =  (y+centro) * width + (x+centro);
			imagenFiltrada[ indice] = valorAux;
		}

	}

}

long int Sustraccion(unsigned char *imagenGrayBase, unsigned char *imagenGrayMov, uint32_t width, uint32_t height )
{
	long int MovTamanio = 0;
	int sustraccion;
	register int i;
	for(i = 0; i < (width*height); i++)
	{
		sustraccion = imagenGrayBase [i] - imagenGrayMov [i];
		imagenResultado[i]=( sustraccion <0) ?0: sustraccion ;
		if (sustraccion >= 1)
		{
			MovTamanio++;
		}
	}
	return MovTamanio;
}
//------------------------------------------------------------------------------------
void GrayToRGB( unsigned char *imagenRGB, unsigned char *imagenGray, uint32_t width, uint32_t height )
{	//Forma Lineal
	
	int indiceGray, indiceRGB;
	for( indiceRGB = 0, indiceGray = 0; indiceGray < (width * height); indiceRGB += 3, indiceGray++ )
	{
			imagenRGB [indiceRGB] = imagenGray[indiceGray];
			imagenRGB [indiceRGB+1] = imagenGray[indiceGray];
			imagenRGB [indiceRGB+2] = imagenGray[indiceGray];
	}
}

void RGBToGray( unsigned char *imagenRGB, unsigned char *imagenGray, uint32_t width, uint32_t height )
{	//Forma Lineal
	unsigned char nivelGris;
	int indiceRGB, indiceGray;

	
	
	for( indiceRGB = 0, indiceGray = 0; indiceGray < (width * height); indiceRGB += 3, indiceGray++ )
	{
		
			nivelGris = ((imagenRGB[indiceRGB]*30) + (imagenRGB[indiceRGB+1]*59) + (imagenRGB[indiceRGB+2])*11) /100;
			imagenGray[indiceGray] = nivelGris;
	}
}
unsigned char * calcularKernelGauss( int * factor, int dim, float des ) 
{
	unsigned char * kernelGauss;
	float coef, norm = 0.0;
	int index = 0;
	kernelGauss = (unsigned char *) malloc( dim*dim*sizeof(unsigned char) );
	if( kernelGauss == NULL ) 
	{
		perror("Error al asignar memoria al kernel Gaussiano");
		exit( EXIT_FAILURE );
	} 
	*factor =  0;
	int rango = dim >> 1;
	for( int y = 0; y < dim; y++ )
	{
		for( int x = 0; x < dim; x++ ) 
		{
			coef = expf( -( (x-rango)*(x-rango) + (y-rango)*(y-rango) )/(2.0*des*des) );
			if( !index )
			{
				norm = coef;
			}
			kernelGauss[ index ] = ( unsigned char )(coef/norm);
			*factor += kernelGauss[ index++ ];  
		} 		
	}
	return kernelGauss;
} 
//--------------------------------------------------------------------
unsigned char * reservarMemoria( int tamImagen )
{
	unsigned char *imagen;

	imagen = (unsigned char *)malloc( tamImagen*sizeof(char)  );
	if( imagen == NULL )
	{
		perror("Error al asignar memoria");
		exit(EXIT_FAILURE );
	}
	return imagen;
}

//------------------------------------------------------------------------------------
void iniDemonio()
{
    FILE *apArch;

    pid_t pid = 0;
    pid_t sid = 0;
   
// Se crea el proceso hijo
    pid = fork();
    if( pid == -1 )
    {
        perror("Error al crear el primer proceso hijo\n");
        exit(EXIT_FAILURE);
    }
/*
 * Se termina Proceso padre.
 * Al finalizar el proceso padre el proceso hijo es adoptado por init. 
 * El resultado es que la shell piensa que el comando terminó con éxito, 
 * permitiendo que el proceso hijo se ejecute de manera independiente en segundo plano.
 */
    if( pid )
    {
        printf("Se termina proceso padre, PID del proceso hijo %d \n", pid);
        exit(0);
    }
/* Se restablece el modo de archivo
 * Todos los procesos tiene una máscara que indica que permisos no deben establecerse al crear nuevos archivos. 
 * Así cuando se utilizan llamadas al sistema como open() los permisos especificados se comparan con esta máscara, 
 * desactivando de manera efectiva los que en ella se indiquen.
 * La máscara —denominada umask()— es heredada de padres a hijos por los procesos, por lo que su valor por defecto 
 * será el mismo que el que tenía configurada la shell que lanzó el demonio. Esto significa que el demonio no sabe 
 * que permisos acabarán tenido los archivos que intente crear. Para evitarlo simplemente podemos autorizar todos 
 * los permisos 
 */
    umask(0);
/*
 * se inicia una nueva sesion
 * Cada proceso es miembro de un grupo y estos a su vez se reúnen en sesiones. En cada una de estas hay un proceso 
 * que hace las veces de líder, de tal forma que si muere todos los procesos de la sesión reciben una señal SIGHUP.
 * La idea es que el líder muere cuando se quiere dar la sesión por terminada, por lo que mediante SIGHUP se 
 * notifica al resto de procesos esta circunstancia para que puedan terminar ordenadamente.
 * Obviamente no estamos interesados en que el demonio termine cuando la sesión desde la que fue creado finalice, 
 * por lo que necesitamos crear nuestra propia sesión de la que dicho demonio será el líder.
 */
    sid = setsid();
    if( sid < 0 )
    {
        perror("Error al iniciar sesion");
        exit(EXIT_FAILURE);
    }
// Se realiza un segundo fork para separarnos completamente de la sesion del padre
    pid = fork( );
    if( pid == -1 )
    {
        perror("Error al crear el segundo proceso hijo\n");
        exit(EXIT_FAILURE);
    }
    if( pid )
    {
        printf("PID del segundo proceso hijo %d \n", pid);
        apArch = fopen("/home/pi/demoniospid/sensor.pid", "w");
        fprintf(apArch, "%d", pid);
        fclose(apArch);

        exit(0);
    }
/* 
 * Se cambia el directorio actual por root.
 * Hasta el momento el directorio de trabajo del proceso es el mismo que el de la shell en el momento en el
 * que se ejecutó el comando. Este podría estar dentro de un punto de montaje cualquiera del sistema, por lo
 * que no tenemos garantías de que vaya a seguir estando disponible durante la ejecución del proceso.
 * Por eso es probable que prefiramos cambiar el directorio de trabajo al directorio raíz, ya que podemos
 * estar seguros de que siempre existirá
 */
    chdir("/");
/*
 * Se cierran los flujos de entrada y salida: stdin, stdout, stderr
 * Puesto que un demonio se ejecuta en segundo plano no debe estar conectado a ninguna terminal. 
 * Sin embargo esto plantea la cuestión de cómo indicar condiciones de error, advertencias u otro 
 * tipo de sucesos del programa. Algunos demonios almacenan estos mensajes en archivos específicos 
 * o en su propia base de datos de sucesos. Sin embargo en muchos sistemas existe un servicio especifico 
 * para registrar estos eventos. En lo sistemas basados en UNIX este servicio lo ofrece el demonio Syslog, 
 * al que otros procesos pueden enviar mensajes a través de la función syslog()
 */
    close( STDIN_FILENO  );
    //close( STDOUT_FILENO );
    close( STDERR_FILENO );
}
//------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------

